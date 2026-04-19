/* hash.c — hash module: non-cryptographic and cryptographic hash functions. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/runtime.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"

/* Vendored crypto library for SHA-256/512. */
extern void vigil_sha256(const uint8_t *data, size_t len, uint8_t out[32]);
extern void vigil_sha512(const uint8_t *data, size_t len, uint8_t out[64]);

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

/* ── Multi-return helpers ────────────────────────────────────────── */

static vigil_status_t push_str_and_ok(vigil_vm_t *vm, const char *text, size_t len, vigil_error_t *error)
{
    vigil_status_t s = push_str(vm, text, len, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t push_str_and_err(vigil_vm_t *vm, const char *msg, vigil_error_t *error)
{
    vigil_status_t s = push_str(vm, "", 0, error);
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

static void to_hex(const uint8_t *data, size_t len, char *out)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++)
    {
        out[i * 2] = hex[data[i] >> 4];
        out[i * 2 + 1] = hex[data[i] & 0x0F];
    }
}

/* ── MD5 (RFC 1321) ──────────────────────────────────────────────── */

static const uint32_t md5_k[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};
static const uint32_t md5_s[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                   5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                   4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                   6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

static uint32_t load_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void store_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void store_le64(uint8_t *p, uint64_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
    p[4] = (uint8_t)(v >> 32); p[5] = (uint8_t)(v >> 40); p[6] = (uint8_t)(v >> 48); p[7] = (uint8_t)(v >> 56);
}

static void md5_hash(const uint8_t *data, size_t len, uint8_t out[16], vigil_allocator_t *alloc)
{
    uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    size_t padded_len = ((len + 8) / 64 + 1) * 64;
    uint8_t *msg = (uint8_t *)alloc->allocate(alloc->user_data, padded_len);
    memset(msg, 0, padded_len);
    memcpy(msg, data, len);
    msg[len] = 0x80;
    uint64_t bit_len = (uint64_t)len * 8;
    store_le64(msg + padded_len - 8, bit_len);

    for (size_t offset = 0; offset < padded_len; offset += 64)
    {
        uint32_t m[16];
        for (int j = 0; j < 16; j++)
            m[j] = load_le32(msg + offset + (size_t)j * 4);
        uint32_t a = a0, b = b0, c = c0, d = d0;
        for (int i = 0; i < 64; i++)
        {
            uint32_t f, g;
            if (i < 16)
            {
                f = (b & c) | (~b & d);
                g = (uint32_t)i;
            }
            else if (i < 32)
            {
                f = (d & b) | (~d & c);
                g = (uint32_t)(5 * i + 1) % 16;
            }
            else if (i < 48)
            {
                f = b ^ c ^ d;
                g = (uint32_t)(3 * i + 5) % 16;
            }
            else
            {
                f = c ^ (b | ~d);
                g = (uint32_t)(7 * i) % 16;
            }
            uint32_t temp = d;
            d = c;
            c = b;
            uint32_t x = a + f + md5_k[i] + m[g];
            b = b + ((x << md5_s[i]) | (x >> (32 - md5_s[i])));
            a = temp;
        }
        a0 += a;
        b0 += b;
        c0 += c;
        d0 += d;
    }
    alloc->deallocate(alloc->user_data, msg);
    store_le32(out, a0);
    store_le32(out + 4, b0);
    store_le32(out + 8, c0);
    store_le32(out + 12, d0);
}

/* ── SHA-1 (FIPS 180-4) ─────────────────────────────────────────── */

static void sha1_hash(const uint8_t *data, size_t len, uint8_t out[20], vigil_allocator_t *alloc)
{
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
    size_t padded_len = ((len + 8) / 64 + 1) * 64;
    uint8_t *msg = (uint8_t *)alloc->allocate(alloc->user_data, padded_len);
    memset(msg, 0, padded_len);
    memcpy(msg, data, len);
    msg[len] = 0x80;
    uint64_t bit_len = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++)
        msg[padded_len - 1 - i] = (uint8_t)(bit_len >> (i * 8));

    for (size_t offset = 0; offset < padded_len; offset += 64)
    {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)msg[offset + i * 4] << 24) | ((uint32_t)msg[offset + i * 4 + 1] << 16) |
                   ((uint32_t)msg[offset + i * 4 + 2] << 8) | (uint32_t)msg[offset + i * 4 + 3];
        for (int i = 16; i < 80; i++)
        {
            uint32_t x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = (x << 1) | (x >> 31);
        }
        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; i++)
        {
            uint32_t f, k;
            if (i < 20)
            {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            }
            else if (i < 40)
            {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            }
            else if (i < 60)
            {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            }
            else
            {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d;
            d = c;
            c = (b << 30) | (b >> 2);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }
    alloc->deallocate(alloc->user_data, msg);
    for (int i = 0; i < 4; i++)
    {
        out[i] = (uint8_t)(h0 >> (24 - i * 8));
        out[4 + i] = (uint8_t)(h1 >> (24 - i * 8));
        out[8 + i] = (uint8_t)(h2 >> (24 - i * 8));
        out[12 + i] = (uint8_t)(h3 >> (24 - i * 8));
        out[16 + i] = (uint8_t)(h4 >> (24 - i * 8));
    }
}

/* ── Module functions ────────────────────────────────────────────── */

static vigil_status_t hash_fnv1a(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t len;
    if (!get_str(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_value_t val = vigil_nanbox_encode_i32(0);
        return vigil_vm_stack_push(vm, &val, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < len; i++)
    {
        hash ^= (uint64_t)(unsigned char)data[i];
        hash *= UINT64_C(1099511628211);
    }
    vigil_value_t val = vigil_nanbox_encode_int((int64_t)hash);
    return vigil_vm_stack_push(vm, &val, error);
}

static vigil_status_t hash_djb2(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t len;
    if (!get_str(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        vigil_value_t val = vigil_nanbox_encode_i32(0);
        return vigil_vm_stack_push(vm, &val, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    uint64_t hash = 5381;
    for (size_t i = 0; i < len; i++)
        hash = ((hash << 5) + hash) + (uint64_t)(unsigned char)data[i];
    vigil_value_t val = vigil_nanbox_encode_int((int64_t)hash);
    return vigil_vm_stack_push(vm, &val, error);
}

static vigil_status_t hash_md5(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t len;
    if (!get_str(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "md5: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_allocator_t alloc = get_alloc(vm);
    uint8_t digest[16];
    md5_hash((const uint8_t *)data, len, digest, &alloc);
    char hex[32];
    to_hex(digest, 16, hex);
    return push_str_and_ok(vm, hex, 32, error);
}

static vigil_status_t hash_sha1(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t len;
    if (!get_str(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "sha1: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_allocator_t alloc = get_alloc(vm);
    uint8_t digest[20];
    sha1_hash((const uint8_t *)data, len, digest, &alloc);
    char hex[40];
    to_hex(digest, 20, hex);
    return push_str_and_ok(vm, hex, 40, error);
}

static vigil_status_t hash_sha256_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t len;
    if (!get_str(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "sha256: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    uint8_t digest[32];
    vigil_sha256((const uint8_t *)data, len, digest);
    char hex[64];
    to_hex(digest, 32, hex);
    return push_str_and_ok(vm, hex, 64, error);
}

static vigil_status_t hash_sha512_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const char *data;
    size_t len;
    if (!get_str(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_str_and_err(vm, "sha512: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    uint8_t digest[64];
    vigil_sha512((const uint8_t *)data, len, digest);
    char hex[128];
    to_hex(digest, 64, hex);
    return push_str_and_ok(vm, hex, 128, error);
}

/* ── Doc strings ─────────────────────────────────────────────────── */

static const vigil_native_symbol_doc_t doc_module = {
    "Hash functions.",
    "Non-cryptographic (FNV-1a, DJB2) and cryptographic (MD5, SHA-1, SHA-256, SHA-512) hashes.",
    NULL,
};
static const vigil_native_symbol_doc_t doc_fnv1a = {"FNV-1a hash.", "Returns 64-bit integer hash.", "hash.fnv1a(\"hello\")"};
static const vigil_native_symbol_doc_t doc_djb2 = {"DJB2 hash.", "Returns 64-bit integer hash.", "hash.djb2(\"hello\")"};
static const vigil_native_symbol_doc_t doc_md5 = {"MD5 hash.", "Returns 32-character hex string. Not cryptographically secure.", "hash.md5(\"hello\")"};
static const vigil_native_symbol_doc_t doc_sha1 = {"SHA-1 hash.", "Returns 40-character hex string. Not cryptographically secure.", "hash.sha1(\"hello\")"};
static const vigil_native_symbol_doc_t doc_sha256 = {"SHA-256 hash.", "Returns 64-character hex string.", "hash.sha256(\"hello\")"};
static const vigil_native_symbol_doc_t doc_sha512 = {"SHA-512 hash.", "Returns 128-character hex string.", "hash.sha512(\"hello\")"};

/* ── Module descriptor ────────────────────────────────────────────── */

static const int p_str[] = {VIGIL_TYPE_STRING};
static const int str_err_returns[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_ERR};
static const char *const pn_data[] = {"data"};

static const vigil_native_module_function_t hash_functions[] = {
    {"fnv1a", 5U, hash_fnv1a, 1U, p_str, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, pn_data, NULL, NULL, &doc_fnv1a},
    {"djb2", 4U, hash_djb2, 1U, p_str, VIGIL_TYPE_I64, 1U, NULL, 0, NULL, NULL, 0U, pn_data, NULL, NULL, &doc_djb2},
    {"md5", 3U, hash_md5, 1U, p_str, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U, pn_data, NULL, NULL, &doc_md5},
    {"sha1", 4U, hash_sha1, 1U, p_str, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U, pn_data, NULL, NULL, &doc_sha1},
    {"sha256", 6U, hash_sha256_fn, 1U, p_str, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U, pn_data, NULL, NULL, &doc_sha256},
    {"sha512", 6U, hash_sha512_fn, 1U, p_str, VIGIL_TYPE_STRING, 2U, str_err_returns, 0, NULL, NULL, 0U, pn_data, NULL, NULL, &doc_sha512},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_hash = {
    "hash", 4U, hash_functions, sizeof(hash_functions) / sizeof(hash_functions[0]), NULL, 0U, &doc_module, NULL, 0U};
