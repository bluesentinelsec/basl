/* encoding.c — encoding module: hex, base64, base64url, base32 encode/decode.
 *
 * Every encode/decode function returns (string, err) for consistency with
 * the VIGIL multi-return convention.  Decode functions report errors on
 * invalid input instead of silently returning "".
 */

#include <stdint.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/runtime.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"

/* ── Allocator helper ────────────────────────────────────────────── */

static vigil_allocator_t get_alloc(vigil_vm_t *vm)
{
    const vigil_allocator_t *a = vigil_runtime_allocator(vigil_vm_runtime(vm));
    if (a != NULL)
        return *a;
    return vigil_default_allocator();
}

/* ── Stack helpers ───────────────────────────────────────────────── */

static bool get_str(vigil_vm_t *vm, size_t base, size_t idx, const char **s, size_t *len)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    if (!vigil_nanbox_is_object(v))
        return false;
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(v);
    if (!obj || vigil_object_type(obj) != VIGIL_OBJECT_STRING)
        return false;
    *s = vigil_string_object_c_str(obj);
    *len = vigil_string_object_length(obj);
    return true;
}

static vigil_status_t push_string(vigil_vm_t *vm, const char *text, size_t len, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_status_t st = vigil_string_object_new(vigil_vm_runtime(vm), text, len, &obj, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_t val;
    vigil_value_init_object(&val, &obj);
    st = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return st;
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

/* ── Hex ─────────────────────────────────────────────────────────── */

static vigil_status_t enc_hex_encode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t len;
    if (!get_str(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "hex_encode: expected string argument", error);
    }

    vigil_allocator_t alloc = get_alloc(vm);
    size_t out_len = len * 2;
    char *out = (char *)alloc.allocate(alloc.user_data, out_len + 1);
    if (!out)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "hex_encode: allocation failed", error);
    }

    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++)
    {
        unsigned char c = (unsigned char)data[i];
        out[i * 2] = hex[c >> 4];
        out[i * 2 + 1] = hex[c & 0x0F];
    }
    out[out_len] = '\0';

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_status_t st = push_str_and_ok(vm, out, out_len, error);
    alloc.deallocate(alloc.user_data, out);
    return st;
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static vigil_status_t enc_hex_decode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t len;
    if (!get_str(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "hex_decode: expected string argument", error);
    }
    if (len % 2 != 0)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "hex_decode: odd-length hex string", error);
    }

    vigil_allocator_t alloc = get_alloc(vm);
    size_t out_len = len / 2;
    char *out = (char *)alloc.allocate(alloc.user_data, out_len + 1);
    if (!out)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "hex_decode: allocation failed", error);
    }

    for (size_t i = 0; i < out_len; i++)
    {
        int hi = hex_val(data[i * 2]);
        int lo = hex_val(data[i * 2 + 1]);
        if (hi < 0 || lo < 0)
        {
            alloc.deallocate(alloc.user_data, out);
            vigil_vm_stack_pop_n(vm, arg_count);
            return push_str_and_err(vm, "hex_decode: invalid hex character", error);
        }
        out[i] = (char)((hi << 4) | lo);
    }
    out[out_len] = '\0';

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_status_t st = push_str_and_ok(vm, out, out_len, error);
    alloc.deallocate(alloc.user_data, out);
    return st;
}

/* ── Base64 (standard + URL-safe) ────────────────────────────────── */

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char b64url_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static vigil_status_t b64_encode_impl(vigil_vm_t *vm, size_t arg_count, const char *table, bool pad,
                                      const char *fn_name, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t len;
    if (!get_str(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, fn_name, error);
    }

    vigil_allocator_t alloc = get_alloc(vm);
    size_t out_len = ((len + 2) / 3) * 4;
    char *out = (char *)alloc.allocate(alloc.user_data, out_len + 1);
    if (!out)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, fn_name, error);
    }

    size_t j = 0;
    for (size_t i = 0; i < len; i += 3)
    {
        unsigned int n = ((unsigned char)data[i]) << 16;
        if (i + 1 < len)
            n |= ((unsigned char)data[i + 1]) << 8;
        if (i + 2 < len)
            n |= (unsigned char)data[i + 2];
        out[j++] = table[(n >> 18) & 0x3F];
        out[j++] = table[(n >> 12) & 0x3F];
        out[j++] = (i + 1 < len) ? table[(n >> 6) & 0x3F] : (pad ? '=' : '\0');
        out[j++] = (i + 2 < len) ? table[n & 0x3F] : (pad ? '=' : '\0');
    }

    /* Trim trailing NULs when not padding. */
    if (!pad)
    {
        while (j > 0 && out[j - 1] == '\0')
            j--;
    }
    out[j] = '\0';

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_status_t st = push_str_and_ok(vm, out, j, error);
    alloc.deallocate(alloc.user_data, out);
    return st;
}

static int b64_val(char c, bool url_safe)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (url_safe)
    {
        if (c == '-')
            return 62;
        if (c == '_')
            return 63;
    }
    else
    {
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
    }
    return -1;
}

static vigil_status_t b64_decode_impl(vigil_vm_t *vm, size_t arg_count, bool url_safe, const char *fn_name,
                                      vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t len;
    if (!get_str(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, fn_name, error);
    }

    /* Strip padding. */
    size_t raw_len = len;
    while (raw_len > 0 && data[raw_len - 1] == '=')
        raw_len--;

    /* Validate all characters before allocating. */
    for (size_t i = 0; i < raw_len; i++)
    {
        if (b64_val(data[i], url_safe) < 0)
        {
            vigil_vm_stack_pop_n(vm, arg_count);
            return push_str_and_err(vm, fn_name, error);
        }
    }

    vigil_allocator_t alloc = get_alloc(vm);
    size_t out_cap = (raw_len * 3) / 4 + 3;
    char *out = (char *)alloc.allocate(alloc.user_data, out_cap);
    if (!out)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, fn_name, error);
    }

    size_t j = 0;
    for (size_t i = 0; i < raw_len; i += 4)
    {
        int a = (i < raw_len) ? b64_val(data[i], url_safe) : 0;
        int b = (i + 1 < raw_len) ? b64_val(data[i + 1], url_safe) : 0;
        int c = (i + 2 < raw_len) ? b64_val(data[i + 2], url_safe) : 0;
        int d = (i + 3 < raw_len) ? b64_val(data[i + 3], url_safe) : 0;
        unsigned int n = ((unsigned)a << 18) | ((unsigned)b << 12) | ((unsigned)c << 6) | (unsigned)d;
        out[j++] = (char)((n >> 16) & 0xFF);
        if (i + 2 < raw_len)
            out[j++] = (char)((n >> 8) & 0xFF);
        if (i + 3 < raw_len)
            out[j++] = (char)(n & 0xFF);
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_status_t st = push_str_and_ok(vm, out, j, error);
    alloc.deallocate(alloc.user_data, out);
    return st;
}

static vigil_status_t enc_base64_encode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return b64_encode_impl(vm, arg_count, b64_table, true, "base64_encode: expected string argument", error);
}

static vigil_status_t enc_base64_decode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return b64_decode_impl(vm, arg_count, false, "base64_decode: invalid base64 input", error);
}

static vigil_status_t enc_base64url_encode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return b64_encode_impl(vm, arg_count, b64url_table, false, "base64url_encode: expected string argument", error);
}

static vigil_status_t enc_base64url_decode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    return b64_decode_impl(vm, arg_count, true, "base64url_decode: invalid base64url input", error);
}

/* ── Base32 ──────────────────────────────────────────────────────── */

static const char b32_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static vigil_status_t enc_base32_encode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t len;
    if (!get_str(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "base32_encode: expected string argument", error);
    }

    vigil_allocator_t alloc = get_alloc(vm);
    size_t out_len = ((len + 4) / 5) * 8;
    char *out = (char *)alloc.allocate(alloc.user_data, out_len + 1);
    if (!out)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "base32_encode: allocation failed", error);
    }
    memset(out, 0, out_len + 1);

    size_t j = 0;
    for (size_t i = 0; i < len; i += 5)
    {
        uint64_t buf = 0;
        size_t avail = (len - i < 5) ? len - i : 5;
        for (size_t k = 0; k < avail; k++)
            buf |= (uint64_t)(unsigned char)data[i + k] << (32 - k * 8);
        size_t chars = (avail * 8 + 4) / 5;
        for (size_t k = 0; k < 8; k++)
        {
            if (k < chars)
                out[j++] = b32_table[(buf >> (35 - k * 5)) & 0x1F];
            else
                out[j++] = '=';
        }
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_status_t st = push_str_and_ok(vm, out, j, error);
    alloc.deallocate(alloc.user_data, out);
    return st;
}

static int b32_val(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a';
    if (c >= '2' && c <= '7')
        return c - '2' + 26;
    return -1;
}

static vigil_status_t enc_base32_decode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t len;
    if (!get_str(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "base32_decode: expected string argument", error);
    }

    /* Strip padding. */
    size_t raw_len = len;
    while (raw_len > 0 && data[raw_len - 1] == '=')
        raw_len--;

    /* Validate all characters. */
    for (size_t i = 0; i < raw_len; i++)
    {
        if (b32_val(data[i]) < 0)
        {
            vigil_vm_stack_pop_n(vm, arg_count);
            return push_str_and_err(vm, "base32_decode: invalid base32 character", error);
        }
    }

    vigil_allocator_t alloc = get_alloc(vm);
    size_t out_cap = raw_len * 5 / 8 + 1;
    char *out = (char *)alloc.allocate(alloc.user_data, out_cap);
    if (!out)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "base32_decode: allocation failed", error);
    }

    uint64_t buf = 0;
    int bits = 0;
    size_t j = 0;
    for (size_t i = 0; i < raw_len; i++)
    {
        buf = (buf << 5) | (uint64_t)b32_val(data[i]);
        bits += 5;
        if (bits >= 8)
        {
            bits -= 8;
            out[j++] = (char)((buf >> bits) & 0xFF);
        }
    }

    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_status_t st = push_str_and_ok(vm, out, j, error);
    alloc.deallocate(alloc.user_data, out);
    return st;
}

/* ── Module descriptor ────────────────────────────────────────────── */

static const int p_str[] = {VIGIL_TYPE_STRING};
static const int str_err_returns[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_ERR};

static const char *const pn_data[] = {"data"};

/* ── Documentation ───────────────────────────────────────────────── */

static const vigil_native_symbol_doc_t encoding_module_doc = {
    "Encoding and decoding utilities.",
    "The encoding module provides hex, base64, base64url, and base32 encoding and decoding. "
    "All functions return (string, err) pairs.",
    NULL,
};

static const vigil_native_symbol_doc_t doc_hex_encode = {
    "Encode bytes to hex string.",
    "Returns the lowercase hexadecimal representation of the input.",
    "string hex, err e = encoding.hex_encode(data)",
};

static const vigil_native_symbol_doc_t doc_hex_decode = {
    "Decode hex string to bytes.",
    "Returns the decoded bytes. Fails on odd-length input or invalid hex characters.",
    "string bytes, err e = encoding.hex_decode(hex)",
};

static const vigil_native_symbol_doc_t doc_base64_encode = {
    "Encode bytes to base64.",
    "Returns the standard base64 encoding with padding.",
    "string b64, err e = encoding.base64_encode(data)",
};

static const vigil_native_symbol_doc_t doc_base64_decode = {
    "Decode base64 string to bytes.",
    "Returns the decoded bytes. Fails on invalid base64 characters.",
    "string bytes, err e = encoding.base64_decode(b64)",
};

static const vigil_native_symbol_doc_t doc_base64url_encode = {
    "Encode bytes to URL-safe base64.",
    "Returns base64url encoding (uses - and _ instead of + and /) without padding.",
    "string b64, err e = encoding.base64url_encode(data)",
};

static const vigil_native_symbol_doc_t doc_base64url_decode = {
    "Decode URL-safe base64 string to bytes.",
    "Returns the decoded bytes. Accepts base64url input with - and _ characters.",
    "string bytes, err e = encoding.base64url_decode(b64)",
};

static const vigil_native_symbol_doc_t doc_base32_encode = {
    "Encode bytes to base32.",
    "Returns the RFC 4648 base32 encoding with padding.",
    "string b32, err e = encoding.base32_encode(data)",
};

static const vigil_native_symbol_doc_t doc_base32_decode = {
    "Decode base32 string to bytes.",
    "Returns the decoded bytes. Fails on invalid base32 characters.",
    "string bytes, err e = encoding.base32_decode(b32)",
};

/* ── Function table ──────────────────────────────────────────────── */

static const vigil_native_module_function_t encoding_functions[] = {
    {"hex_encode", 10U, enc_hex_encode, 1U, p_str, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U, pn_data,
     NULL, NULL, &doc_hex_encode},
    {"hex_decode", 10U, enc_hex_decode, 1U, p_str, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U, pn_data,
     NULL, NULL, &doc_hex_decode},
    {"base64_encode", 13U, enc_base64_encode, 1U, p_str, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U,
     pn_data, NULL, NULL, &doc_base64_encode},
    {"base64_decode", 13U, enc_base64_decode, 1U, p_str, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U,
     pn_data, NULL, NULL, &doc_base64_decode},
    {"base64url_encode", 16U, enc_base64url_encode, 1U, p_str, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL,
     0U, pn_data, NULL, NULL, &doc_base64url_encode},
    {"base64url_decode", 16U, enc_base64url_decode, 1U, p_str, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL,
     0U, pn_data, NULL, NULL, &doc_base64url_decode},
    {"base32_encode", 13U, enc_base32_encode, 1U, p_str, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U,
     pn_data, NULL, NULL, &doc_base32_encode},
    {"base32_decode", 13U, enc_base32_decode, 1U, p_str, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U,
     pn_data, NULL, NULL, &doc_base32_decode},
};

#define ENCODING_FUNCTION_COUNT (sizeof(encoding_functions) / sizeof(encoding_functions[0]))

VIGIL_API const vigil_native_module_t vigil_stdlib_encoding = {
    "encoding", 8U, encoding_functions, ENCODING_FUNCTION_COUNT, NULL, 0U, &encoding_module_doc, NULL, 0U};
