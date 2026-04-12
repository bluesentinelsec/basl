/* encoding.c — encoding module: hex, base64, base32 encode/decode. */

#include <stdlib.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_nanbox.h"

/* ── Helpers ─────────────────────────────────────────────────────── */

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

static vigil_status_t push_str(vigil_vm_t *vm, const char *text, size_t len, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_value_t val;
    vigil_status_t st = vigil_string_object_new(vigil_vm_runtime(vm), text, len, &obj, error);
    if (st != VIGIL_STATUS_OK)
        return st;
    vigil_value_init_object(&val, &obj);
    st = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return st;
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
        return push_str(vm, "", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    size_t out_len = len * 2;
    char *out = (char *)malloc(out_len + 1);
    if (!out)
        return push_str(vm, "", 0, error);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++)
    {
        unsigned char c = (unsigned char)data[i];
        out[i * 2] = hex[c >> 4];
        out[i * 2 + 1] = hex[c & 0x0F];
    }
    out[out_len] = '\0';
    vigil_status_t st = push_str(vm, out, out_len, error);
    free(out);
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
    if (!get_str(vm, base, 0, &data, &len) || len % 2 != 0)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str(vm, "", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    size_t out_len = len / 2;
    char *out = (char *)malloc(out_len + 1);
    if (!out)
        return push_str(vm, "", 0, error);
    for (size_t i = 0; i < out_len; i++)
    {
        int hi = hex_val(data[i * 2]);
        int lo = hex_val(data[i * 2 + 1]);
        if (hi < 0 || lo < 0)
        {
            free(out);
            return push_str(vm, "", 0, error);
        }
        out[i] = (char)((hi << 4) | lo);
    }
    out[out_len] = '\0';
    vigil_status_t st = push_str(vm, out, out_len, error);
    free(out);
    return st;
}

/* ── Base64 ──────────────────────────────────────────────────────── */

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static vigil_status_t enc_base64_encode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t len;
    if (!get_str(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str(vm, "", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    size_t out_len = ((len + 2) / 3) * 4;
    char *out = (char *)malloc(out_len + 1);
    if (!out)
        return push_str(vm, "", 0, error);
    size_t j = 0;
    for (size_t i = 0; i < len; i += 3)
    {
        unsigned int n = ((unsigned char)data[i]) << 16;
        if (i + 1 < len)
            n |= ((unsigned char)data[i + 1]) << 8;
        if (i + 2 < len)
            n |= (unsigned char)data[i + 2];
        out[j++] = b64_table[(n >> 18) & 0x3F];
        out[j++] = b64_table[(n >> 12) & 0x3F];
        out[j++] = (i + 1 < len) ? b64_table[(n >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < len) ? b64_table[n & 0x3F] : '=';
    }
    out[j] = '\0';
    vigil_status_t st = push_str(vm, out, j, error);
    free(out);
    return st;
}

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

static vigil_status_t enc_base64_decode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t len;
    if (!get_str(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str(vm, "", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    size_t out_cap = (len / 4) * 3 + 3;
    char *out = (char *)malloc(out_cap);
    if (!out)
        return push_str(vm, "", 0, error);
    size_t j = 0;
    for (size_t i = 0; i < len; i += 4)
    {
        int a = (i < len) ? b64_val(data[i]) : 0;
        int b = (i + 1 < len) ? b64_val(data[i + 1]) : 0;
        int c = (i + 2 < len) ? b64_val(data[i + 2]) : 0;
        int d = (i + 3 < len) ? b64_val(data[i + 3]) : 0;
        if (a < 0)
            a = 0;
        if (b < 0)
            b = 0;
        if (c < 0)
            c = 0;
        if (d < 0)
            d = 0;
        unsigned int n = ((unsigned)a << 18) | ((unsigned)b << 12) | ((unsigned)c << 6) | (unsigned)d;
        out[j++] = (char)((n >> 16) & 0xFF);
        if (i + 2 < len && data[i + 2] != '=')
            out[j++] = (char)((n >> 8) & 0xFF);
        if (i + 3 < len && data[i + 3] != '=')
            out[j++] = (char)(n & 0xFF);
    }
    vigil_status_t st = push_str(vm, out, j, error);
    free(out);
    return st;
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
        return push_str(vm, "", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    size_t out_len = ((len + 4) / 5) * 8;
    char *out = (char *)calloc(1, out_len + 1);
    if (!out)
        return push_str(vm, "", 0, error);
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
    vigil_status_t st = push_str(vm, out, j, error);
    free(out);
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
        return push_str(vm, "", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    /* Strip padding. */
    while (len > 0 && data[len - 1] == '=')
        len--;
    size_t out_cap = len * 5 / 8 + 1;
    char *out = (char *)calloc(1, out_cap);
    if (!out)
        return push_str(vm, "", 0, error);
    uint64_t buf = 0;
    int bits = 0;
    size_t j = 0;
    for (size_t i = 0; i < len; i++)
    {
        int v = b32_val(data[i]);
        if (v < 0)
            continue;
        buf = (buf << 5) | (uint64_t)v;
        bits += 5;
        if (bits >= 8)
        {
            bits -= 8;
            out[j++] = (char)((buf >> bits) & 0xFF);
        }
    }
    vigil_status_t st = push_str(vm, out, j, error);
    free(out);
    return st;
}

/* ── Module descriptor ────────────────────────────────────────────── */

static const int p_str[] = {VIGIL_TYPE_STRING};

static const vigil_native_module_function_t encoding_functions[] = {
    {"hex_encode", 10U, enc_hex_encode, 1U, p_str, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    {"hex_decode", 10U, enc_hex_decode, 1U, p_str, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL, NULL,
     NULL},
    {"base64_encode", 13U, enc_base64_encode, 1U, p_str, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
    {"base64_decode", 13U, enc_base64_decode, 1U, p_str, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
    {"base32_encode", 13U, enc_base32_encode, 1U, p_str, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
    {"base32_decode", 13U, enc_base32_decode, 1U, p_str, VIGIL_TYPE_STRING, 1U, NULL, 0, NULL, NULL, 0U, NULL, NULL,
     NULL, NULL},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_encoding = {
    "encoding", 8U, encoding_functions, sizeof(encoding_functions) / sizeof(encoding_functions[0]), NULL, 0U, NULL,
    NULL,       0U};
