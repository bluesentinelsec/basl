/* VIGIL standard library: crypto module.
 *
 * Provides cryptographic operations:
 * - Hashing: SHA-256, SHA-384, SHA-512
 * - HMAC: HMAC-SHA256
 * - Encryption: AES-256-GCM
 * - Key derivation: PBKDF2
 * - Utilities: random bytes, constant-time compare, hex/base64
 */

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdlib.h>
#include <string.h>

#include "vigil/native_module.h"
#include "vigil/runtime.h"
#include "vigil/type.h"
#include "vigil/value.h"
#include "vigil/vm.h"

#include "internal/vigil_internal.h"
#include "internal/vigil_nanbox.h"
#include "vigil_crypto.h"

/* ── Allocator helpers ───────────────────────────────────────────── */

static vigil_allocator_t get_alloc(vigil_vm_t *vm)
{
    const vigil_allocator_t *a = vigil_runtime_allocator(vigil_vm_runtime(vm));
    if (a != NULL)
        return *a;
    return vigil_default_allocator();
}

/* ── Helpers ─────────────────────────────────────────────────────── */

static bool get_bytes_arg(vigil_vm_t *vm, size_t base, size_t idx, const uint8_t **out, size_t *out_len)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    if (!vigil_nanbox_is_object(v))
        return false;
    vigil_object_t *obj = (vigil_object_t *)vigil_nanbox_decode_ptr(v);
    if (!obj || vigil_object_type(obj) != VIGIL_OBJECT_STRING)
        return false;
    *out = (const uint8_t *)vigil_string_object_c_str(obj);
    *out_len = vigil_string_object_length(obj);
    return true;
}

static int32_t get_i32_arg(vigil_vm_t *vm, size_t base, size_t idx)
{
    vigil_value_t v = vigil_vm_stack_get(vm, base + idx);
    return vigil_nanbox_decode_i32(v);
}

static vigil_status_t push_bytes(vigil_vm_t *vm, const uint8_t *data, size_t len, vigil_error_t *error)
{
    vigil_object_t *obj = NULL;
    vigil_status_t s = vigil_string_object_new(vigil_vm_runtime(vm), (const char *)data, len, &obj, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    vigil_value_t val;
    vigil_value_init_object(&val, &obj);
    s = vigil_vm_stack_push(vm, &val, error);
    vigil_value_release(&val);
    return s;
}

static vigil_status_t push_bool(vigil_vm_t *vm, int val, vigil_error_t *error)
{
    vigil_value_t v;
    vigil_value_init_bool(&v, val);
    return vigil_vm_stack_push(vm, &v, error);
}

/* ── Hex encoding ────────────────────────────────────────────────── */

static const char hex_chars[] = "0123456789abcdef";

static void bytes_to_hex(const uint8_t *data, size_t len, char *out)
{
    for (size_t i = 0; i < len; i++)
    {
        out[i * 2] = hex_chars[data[i] >> 4];
        out[i * 2 + 1] = hex_chars[data[i] & 0xf];
    }
}

static int hex_digit_value(int ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

static int hex_to_bytes(const char *hex, size_t hex_len, uint8_t *out)
{
    if (hex_len % 2 != 0)
        return -1;
    for (size_t i = 0; i < hex_len / 2; i++)
    {
        int hi = hex_digit_value(hex[i * 2]);
        int lo = hex_digit_value(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

/* ── Base64 encoding ─────────────────────────────────────────────── */

static const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t base64_encode(const uint8_t *data, size_t len, char *out)
{
    size_t i, j = 0;
    for (i = 0; i + 2 < len; i += 3)
    {
        out[j++] = b64_chars[data[i] >> 2];
        out[j++] = b64_chars[((data[i] & 3) << 4) | (data[i + 1] >> 4)];
        out[j++] = b64_chars[((data[i + 1] & 15) << 2) | (data[i + 2] >> 6)];
        out[j++] = b64_chars[data[i + 2] & 63];
    }
    if (i < len)
    {
        out[j++] = b64_chars[data[i] >> 2];
        if (i + 1 < len)
        {
            out[j++] = b64_chars[((data[i] & 3) << 4) | (data[i + 1] >> 4)];
            out[j++] = b64_chars[(data[i + 1] & 15) << 2];
        }
        else
        {
            out[j++] = b64_chars[(data[i] & 3) << 4];
            out[j++] = '=';
        }
        out[j++] = '=';
    }
    return j;
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

static size_t base64_decode(const char *data, size_t len, uint8_t *out)
{
    size_t i, j = 0;
    for (i = 0; i + 3 < len; i += 4)
    {
        if (data[i + 2] == '=' && data[i + 3] == '=')
        {
            int a = b64_val(data[i]), b = b64_val(data[i + 1]);
            if (a < 0 || b < 0)
                return 0;
            out[j++] = (uint8_t)((a << 2) | (b >> 4));
            break;
        }
        else if (data[i + 3] == '=')
        {
            int a = b64_val(data[i]), b = b64_val(data[i + 1]), c = b64_val(data[i + 2]);
            if (a < 0 || b < 0 || c < 0)
                return 0;
            out[j++] = (uint8_t)((a << 2) | (b >> 4));
            out[j++] = (uint8_t)((b << 4) | (c >> 2));
            break;
        }
        else
        {
            int a = b64_val(data[i]), b = b64_val(data[i + 1]);
            int c = b64_val(data[i + 2]), d = b64_val(data[i + 3]);
            if (a < 0 || b < 0 || c < 0 || d < 0)
                return 0;
            out[j++] = (uint8_t)((a << 2) | (b >> 4));
            out[j++] = (uint8_t)((b << 4) | (c >> 2));
            out[j++] = (uint8_t)((c << 6) | d);
        }
    }
    return j;
}

/* ── crypto.sha256(data: string) -> string ───────────────────────── */

static vigil_status_t crypto_sha256(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *data;
    size_t len;
    uint8_t hash[32];
    char hex[64];

    if (!get_bytes_arg(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_sha256(data, len, hash);
    bytes_to_hex(hash, 32, hex);
    return push_bytes(vm, (uint8_t *)hex, 64, error);
}

/* ── crypto.sha384(data: string) -> string ───────────────────────── */

static vigil_status_t crypto_sha384(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *data;
    size_t len;
    uint8_t hash[48];
    char hex[96];

    if (!get_bytes_arg(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_sha384(data, len, hash);
    bytes_to_hex(hash, 48, hex);
    return push_bytes(vm, (uint8_t *)hex, 96, error);
}

/* ── crypto.sha512(data: string) -> string ───────────────────────── */

static vigil_status_t crypto_sha512(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *data;
    size_t len;
    uint8_t hash[64];
    char hex[128];

    if (!get_bytes_arg(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_sha512(data, len, hash);
    bytes_to_hex(hash, 64, hex);
    return push_bytes(vm, (uint8_t *)hex, 128, error);
}

/* ── crypto.hmac_sha256(key: string, data: string) -> string ─────── */

static vigil_status_t crypto_hmac_sha256(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *key, *data;
    size_t key_len, data_len;
    uint8_t mac[32];
    char hex[64];

    if (!get_bytes_arg(vm, base, 0, &key, &key_len) || !get_bytes_arg(vm, base, 1, &data, &data_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_hmac_sha256(key, key_len, data, data_len, mac);
    bytes_to_hex(mac, 32, hex);
    return push_bytes(vm, (uint8_t *)hex, 64, error);
}

/* ── crypto.pbkdf2(password, salt, iterations, key_len) -> string ── */

static vigil_status_t crypto_pbkdf2(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *password, *salt;
    size_t pass_len, salt_len;
    int32_t iterations, key_len;

    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &password, &pass_len) || !get_bytes_arg(vm, base, 1, &salt, &salt_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }
    iterations = get_i32_arg(vm, base, 2);
    key_len = get_i32_arg(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (iterations < 1 || key_len < 1 || key_len > 1024)
    {
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }

    uint8_t *key = (uint8_t *)alloc.allocate(alloc.user_data, (size_t)key_len);
    if (!key)
        return push_bytes(vm, (uint8_t *)"", 0, error);

    vigil_pbkdf2_sha256(password, pass_len, salt, salt_len, (uint32_t)iterations, key, (size_t)key_len);

    char *hex = (char *)alloc.allocate(alloc.user_data, (size_t)key_len * 2);
    if (!hex)
    {
        alloc.deallocate(alloc.user_data, key);
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }
    bytes_to_hex(key, (size_t)key_len, hex);

    vigil_status_t s = push_bytes(vm, (uint8_t *)hex, (size_t)key_len * 2, error);
    alloc.deallocate(alloc.user_data, key);
    alloc.deallocate(alloc.user_data, hex);
    return s;
}

/* ── crypto.random_bytes(len: i32) -> string ─────────────────────── */

static vigil_status_t crypto_random_bytes(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t len = get_i32_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_allocator_t alloc = get_alloc(vm);

    if (len < 0 || len > 65536)
    {
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }

    uint8_t *buf = (uint8_t *)alloc.allocate(alloc.user_data, (size_t)len);
    if (!buf)
        return push_bytes(vm, (uint8_t *)"", 0, error);

    vigil_crypto_random_bytes(buf, (size_t)len);
    vigil_status_t s = push_bytes(vm, buf, (size_t)len, error);
    alloc.deallocate(alloc.user_data, buf);
    return s;
}

/* ── crypto.constant_time_eq(a, b) -> bool ───────────────────────── */

static vigil_status_t crypto_constant_time_eq(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *a, *b;
    size_t a_len, b_len;

    if (!get_bytes_arg(vm, base, 0, &a, &a_len) || !get_bytes_arg(vm, base, 1, &b, &b_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (a_len != b_len)
        return push_bool(vm, 0, error);
    return push_bool(vm, vigil_crypto_constant_time_compare(a, b, a_len), error);
}

/* ── crypto.encrypt(key, nonce, plaintext, aad?) -> string ───────── */

static vigil_status_t crypto_encrypt(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *key, *nonce, *plaintext, *aad = NULL;
    size_t key_len, nonce_len, pt_len, aad_len = 0;

    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &key, &key_len) || !get_bytes_arg(vm, base, 1, &nonce, &nonce_len) ||
        !get_bytes_arg(vm, base, 2, &plaintext, &pt_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }
    if (arg_count > 3)
        get_bytes_arg(vm, base, 3, &aad, &aad_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (key_len != 32)
        return push_bytes(vm, (uint8_t *)"", 0, error);

    /* Output: nonce || ciphertext || tag */
    size_t out_len = nonce_len + pt_len + 16;
    uint8_t *out = (uint8_t *)alloc.allocate(alloc.user_data, out_len);
    if (!out)
        return push_bytes(vm, (uint8_t *)"", 0, error);

    memcpy(out, nonce, nonce_len);
    vigil_aes256_gcm_encrypt(key, nonce, nonce_len, plaintext, pt_len, aad, aad_len, out + nonce_len,
                             out + nonce_len + pt_len);

    vigil_status_t s = push_bytes(vm, out, out_len, error);
    alloc.deallocate(alloc.user_data, out);
    return s;
}

/* ── crypto.decrypt(key, ciphertext, aad?) -> string ─────────────── */

static vigil_status_t crypto_decrypt(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *key, *ciphertext, *aad = NULL;
    size_t key_len, ct_len, aad_len = 0;

    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &key, &key_len) || !get_bytes_arg(vm, base, 1, &ciphertext, &ct_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }
    if (arg_count > 2)
        get_bytes_arg(vm, base, 2, &aad, &aad_len);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (key_len != 32 || ct_len < 12 + 16)
    {
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }

    /* Input: nonce (12) || ciphertext || tag (16) */
    const uint8_t *nonce = ciphertext;
    size_t nonce_len = 12;
    const uint8_t *ct = ciphertext + 12;
    size_t pt_len = ct_len - 12 - 16;
    const uint8_t *tag = ciphertext + ct_len - 16;

    uint8_t *plaintext = (uint8_t *)alloc.allocate(alloc.user_data, pt_len > 0 ? pt_len : 1);
    if (!plaintext)
        return push_bytes(vm, (uint8_t *)"", 0, error);

    int result = vigil_aes256_gcm_decrypt(key, nonce, nonce_len, ct, pt_len, aad, aad_len, tag, plaintext);
    if (result != 0)
    {
        alloc.deallocate(alloc.user_data, plaintext);
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }

    vigil_status_t s = push_bytes(vm, plaintext, pt_len, error);
    alloc.deallocate(alloc.user_data, plaintext);
    return s;
}

/* ── crypto.hex_encode(data) -> string ───────────────────────────── */

static vigil_status_t crypto_hex_encode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *data;
    size_t len;

    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    char *hex = (char *)alloc.allocate(alloc.user_data, len * 2);
    if (!hex)
        return push_bytes(vm, (uint8_t *)"", 0, error);
    bytes_to_hex(data, len, hex);
    vigil_status_t s = push_bytes(vm, (uint8_t *)hex, len * 2, error);
    alloc.deallocate(alloc.user_data, hex);
    return s;
}

/* ── crypto.hex_decode(hex) -> string ────────────────────────────── */

static vigil_status_t crypto_hex_decode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *hex;
    size_t len;

    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &hex, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (len % 2 != 0)
        return push_bytes(vm, (uint8_t *)"", 0, error);

    uint8_t *out = (uint8_t *)alloc.allocate(alloc.user_data, len / 2);
    if (!out)
        return push_bytes(vm, (uint8_t *)"", 0, error);
    if (hex_to_bytes((const char *)hex, len, out) != 0)
    {
        alloc.deallocate(alloc.user_data, out);
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }
    vigil_status_t s = push_bytes(vm, out, len / 2, error);
    alloc.deallocate(alloc.user_data, out);
    return s;
}

/* ── crypto.base64_encode(data) -> string ────────────────────────── */

static vigil_status_t crypto_base64_encode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *data;
    size_t len;

    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    size_t out_len = ((len + 2) / 3) * 4;
    char *out = (char *)alloc.allocate(alloc.user_data, out_len);
    if (!out)
        return push_bytes(vm, (uint8_t *)"", 0, error);
    size_t actual = base64_encode(data, len, out);
    vigil_status_t s = push_bytes(vm, (uint8_t *)out, actual, error);
    alloc.deallocate(alloc.user_data, out);
    return s;
}

/* ── crypto.base64_decode(data) -> string ────────────────────────── */

static vigil_status_t crypto_base64_decode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *data;
    size_t len;

    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    size_t out_len = (len / 4) * 3;
    uint8_t *out = (uint8_t *)alloc.allocate(alloc.user_data, out_len > 0 ? out_len : 1);
    if (!out)
        return push_bytes(vm, (uint8_t *)"", 0, error);
    size_t actual = base64_decode((const char *)data, len, out);
    vigil_status_t s = push_bytes(vm, out, actual, error);
    alloc.deallocate(alloc.user_data, out);
    return s;
}

/* ── crypto.password_encrypt(password, plaintext) -> string ──────── */

static vigil_status_t crypto_password_encrypt(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *password, *plaintext;
    size_t pass_len, pt_len;

    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &password, &pass_len) || !get_bytes_arg(vm, base, 1, &plaintext, &pt_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    uint8_t *out = (uint8_t *)alloc.allocate(alloc.user_data, pt_len + 44);
    if (!out)
        return push_bytes(vm, (uint8_t *)"", 0, error);

    size_t out_len = vigil_password_encrypt(password, pass_len, plaintext, pt_len, out);
    if (out_len == 0)
    {
        alloc.deallocate(alloc.user_data, out);
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }

    vigil_status_t s = push_bytes(vm, out, out_len, error);
    alloc.deallocate(alloc.user_data, out);
    return s;
}

/* ── crypto.password_decrypt(password, ciphertext) -> string ─────── */

static vigil_status_t crypto_password_decrypt(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *password, *ciphertext;
    size_t pass_len, ct_len;

    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &password, &pass_len) || !get_bytes_arg(vm, base, 1, &ciphertext, &ct_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (ct_len < 44)
        return push_bytes(vm, (uint8_t *)"", 0, error);

    uint8_t *out = (uint8_t *)alloc.allocate(alloc.user_data, ct_len);
    if (!out)
        return push_bytes(vm, (uint8_t *)"", 0, error);

    size_t pt_len = vigil_password_decrypt(password, pass_len, ciphertext, ct_len, out);
    if (pt_len == 0)
    {
        alloc.deallocate(alloc.user_data, out);
        return push_bytes(vm, (uint8_t *)"", 0, error);
    }

    vigil_status_t s = push_bytes(vm, out, pt_len, error);
    alloc.deallocate(alloc.user_data, out);
    return s;
}

/* ── Module descriptor ───────────────────────────────────────────── */

static const int crypto_string_params[] = {VIGIL_TYPE_STRING};
static const int crypto_2string_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int crypto_3string_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int crypto_pbkdf2_params[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int crypto_i32_params[] = {VIGIL_TYPE_I32};
static const char *const crypto_data_param_names[] = {"data"};
static const char *const crypto_key_data_param_names[] = {"key", "data"};
static const char *const crypto_pbkdf2_param_names[] = {"password", "salt", "iterations", "key_len"};
static const char *const crypto_len_param_names[] = {"len"};
static const char *const crypto_compare_param_names[] = {"a", "b"};
static const char *const crypto_encrypt_param_names[] = {"key", "nonce", "plaintext"};
static const char *const crypto_decrypt_param_names[] = {"key", "ciphertext"};
static const char *const crypto_password_encrypt_param_names[] = {"password", "plaintext"};
static const char *const crypto_password_decrypt_param_names[] = {"password", "ciphertext"};
static const char *const crypto_hex_param_names[] = {"hex"};

static const vigil_native_symbol_doc_t vigil_crypto_module_doc = {
    "Cryptographic operations.",
    "The crypto module provides secure hashing, encryption, and key derivation.\n"
    "Uses AES-256-GCM for authenticated encryption and SHA-2 for hashing.\n"
    "All functions return hex-encoded strings for hash outputs.",
    NULL,
};

static const vigil_native_symbol_doc_t vigil_crypto_sha256_doc = {
    "SHA-256 hash.",
    "Returns 64-character hex string.",
    "crypto.sha256(\"hello\")",
};

static const vigil_native_symbol_doc_t vigil_crypto_sha384_doc = {
    "SHA-384 hash.",
    "Returns 96-character hex string.",
    "crypto.sha384(\"hello\")",
};

static const vigil_native_symbol_doc_t vigil_crypto_sha512_doc = {
    "SHA-512 hash.",
    "Returns 128-character hex string.",
    "crypto.sha512(\"hello\")",
};

static const vigil_native_symbol_doc_t vigil_crypto_hmac_sha256_doc = {
    "HMAC-SHA256.",
    "Returns 64-character hex string.",
    "crypto.hmac_sha256(\"key\", \"message\")",
};

static const vigil_native_symbol_doc_t vigil_crypto_pbkdf2_doc = {
    "PBKDF2 key derivation.",
    "Returns hex-encoded derived key. Use 100000+ iterations.",
    "crypto.pbkdf2(\"password\", \"salt\", 100000, 32)",
};

static const vigil_native_symbol_doc_t vigil_crypto_random_bytes_doc = {
    "Cryptographically secure random bytes.",
    "Returns raw bytes (not hex). Max 65536 bytes.",
    "crypto.random_bytes(32)",
};

static const vigil_native_symbol_doc_t vigil_crypto_constant_time_eq_doc = {
    "Constant-time comparison.",
    "Prevents timing attacks when comparing secrets.",
    "crypto.constant_time_eq(hash1, hash2)",
};

static const vigil_native_symbol_doc_t vigil_crypto_encrypt_doc = {
    "AES-256-GCM encryption.",
    "Key must be 32 bytes. Returns nonce||ciphertext||tag.",
    "crypto.encrypt(key, nonce, \"secret\")",
};

static const vigil_native_symbol_doc_t vigil_crypto_decrypt_doc = {
    "AES-256-GCM decryption.",
    "Returns empty string on authentication failure.",
    "crypto.decrypt(key, encrypted)",
};

static const vigil_native_symbol_doc_t vigil_crypto_password_encrypt_doc = {
    "Password-based encryption.",
    "Uses PBKDF2 + AES-256-GCM. Password can be any length.",
    "crypto.password_encrypt(\"my password\", \"secret\")",
};

static const vigil_native_symbol_doc_t vigil_crypto_password_decrypt_doc = {
    "Password-based decryption.",
    "Returns empty string on wrong password or auth failure.",
    "crypto.password_decrypt(\"my password\", encrypted)",
};

static const vigil_native_symbol_doc_t vigil_crypto_hex_encode_doc = {
    "Encode bytes as hex.",
    "Returns lowercase hex string.",
    "crypto.hex_encode(\"\\x00\\xff\")",
};

static const vigil_native_symbol_doc_t vigil_crypto_hex_decode_doc = {
    "Decode hex to bytes.",
    "Returns empty string on invalid input.",
    "crypto.hex_decode(\"00ff\")",
};

static const vigil_native_symbol_doc_t vigil_crypto_base64_encode_doc = {
    "Encode bytes as base64.",
    "Standard base64 with padding.",
    "crypto.base64_encode(\"hello\")",
};

static const vigil_native_symbol_doc_t vigil_crypto_base64_decode_doc = {
    "Decode base64 to bytes.",
    "Returns empty string on invalid input.",
    "crypto.base64_decode(\"aGVsbG8=\")",
};

static const vigil_native_module_function_t crypto_functions[] = {
    {"sha256", 6, crypto_sha256, 1, crypto_string_params, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL, 0U,
     crypto_data_param_names, NULL, NULL, &vigil_crypto_sha256_doc},
    {"sha384", 6, crypto_sha384, 1, crypto_string_params, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL, 0U,
     crypto_data_param_names, NULL, NULL, &vigil_crypto_sha384_doc},
    {"sha512", 6, crypto_sha512, 1, crypto_string_params, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL, 0U,
     crypto_data_param_names, NULL, NULL, &vigil_crypto_sha512_doc},
    {"hmac_sha256", 11, crypto_hmac_sha256, 2, crypto_2string_params, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL, 0U,
     crypto_key_data_param_names, NULL, NULL, &vigil_crypto_hmac_sha256_doc},
    {"pbkdf2", 6, crypto_pbkdf2, 4, crypto_pbkdf2_params, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL, 0U,
     crypto_pbkdf2_param_names, NULL, NULL, &vigil_crypto_pbkdf2_doc},
    {"random_bytes", 12, crypto_random_bytes, 1, crypto_i32_params, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL, 0U,
     crypto_len_param_names, NULL, NULL, &vigil_crypto_random_bytes_doc},
    {"constant_time_eq", 16, crypto_constant_time_eq, 2, crypto_2string_params, VIGIL_TYPE_BOOL, 1, NULL, 0, NULL, NULL,
     0U, crypto_compare_param_names, NULL, NULL, &vigil_crypto_constant_time_eq_doc},
    {"encrypt", 7, crypto_encrypt, 3, crypto_3string_params, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL, 0U,
     crypto_encrypt_param_names, NULL, NULL, &vigil_crypto_encrypt_doc},
    {"decrypt", 7, crypto_decrypt, 2, crypto_2string_params, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL, 0U,
     crypto_decrypt_param_names, NULL, NULL, &vigil_crypto_decrypt_doc},
    {"password_encrypt", 16, crypto_password_encrypt, 2, crypto_2string_params, VIGIL_TYPE_STRING, 1, NULL, 0, NULL,
     NULL, 0U, crypto_password_encrypt_param_names, NULL, NULL, &vigil_crypto_password_encrypt_doc},
    {"password_decrypt", 16, crypto_password_decrypt, 2, crypto_2string_params, VIGIL_TYPE_STRING, 1, NULL, 0, NULL,
     NULL, 0U, crypto_password_decrypt_param_names, NULL, NULL, &vigil_crypto_password_decrypt_doc},
    {"hex_encode", 10, crypto_hex_encode, 1, crypto_string_params, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL, 0U,
     crypto_data_param_names, NULL, NULL, &vigil_crypto_hex_encode_doc},
    {"hex_decode", 10, crypto_hex_decode, 1, crypto_string_params, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL, 0U,
     crypto_hex_param_names, NULL, NULL, &vigil_crypto_hex_decode_doc},
    {"base64_encode", 13, crypto_base64_encode, 1, crypto_string_params, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL, 0U,
     crypto_data_param_names, NULL, NULL, &vigil_crypto_base64_encode_doc},
    {"base64_decode", 13, crypto_base64_decode, 1, crypto_string_params, VIGIL_TYPE_STRING, 1, NULL, 0, NULL, NULL, 0U,
     crypto_data_param_names, NULL, NULL, &vigil_crypto_base64_decode_doc},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_crypto = {
    "crypto", 6, crypto_functions,         sizeof(crypto_functions) / sizeof(crypto_functions[0]),
    NULL,     0, &vigil_crypto_module_doc, NULL,
    0U};
