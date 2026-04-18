/* VIGIL standard library: crypto module.
 *
 * Provides cryptographic operations:
 * - Hashing: SHA-224, SHA-256, SHA-384, SHA-512
 * - HMAC: HMAC-SHA256, HMAC-SHA384, HMAC-SHA512
 * - Encryption: AES-256-GCM, AES-128-GCM, ChaCha20-Poly1305, AES-CBC
 * - Key derivation: PBKDF2, HKDF-SHA256
 * - Key exchange: X25519
 * - Signatures: Ed25519
 * - Password-based encryption
 * - Utilities: random bytes, constant-time compare, hex/base64
 *
 * All fallible functions return (result, err) multi-return.
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

/* ── Stack helpers ───────────────────────────────────────────────── */

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

/* ── Multi-return helpers ────────────────────────────────────────── */

static vigil_status_t push_bytes_and_ok(vigil_vm_t *vm, const uint8_t *data, size_t len, vigil_error_t *error)
{
    vigil_status_t s = push_bytes(vm, data, len, error);
    if (s != VIGIL_STATUS_OK)
        return s;
    return vigil_runtime_push_ok_error(vigil_vm_runtime(vm), vm, error);
}

static vigil_status_t push_bytes_and_err(vigil_vm_t *vm, const char *msg, vigil_error_t *error)
{
    vigil_status_t s = push_bytes(vm, (const uint8_t *)"", 0, error);
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

/* ── crypto.sha224(data) -> (string, err) ────────────────────────── */

static vigil_status_t crypto_sha224(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *data;
    size_t len;
    if (!get_bytes_arg(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "sha224: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    uint8_t hash[28];
    char hex[56];
    vigil_sha224(data, len, hash);
    bytes_to_hex(hash, 28, hex);
    return push_bytes_and_ok(vm, (uint8_t *)hex, 56, error);
}

/* ── crypto.sha256(data) -> (string, err) ────────────────────────── */

static vigil_status_t crypto_sha256(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *data;
    size_t len;
    if (!get_bytes_arg(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "sha256: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    uint8_t hash[32];
    char hex[64];
    vigil_sha256(data, len, hash);
    bytes_to_hex(hash, 32, hex);
    return push_bytes_and_ok(vm, (uint8_t *)hex, 64, error);
}

/* ── crypto.sha384(data) -> (string, err) ────────────────────────── */

static vigil_status_t crypto_sha384(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *data;
    size_t len;
    if (!get_bytes_arg(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "sha384: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    uint8_t hash[48];
    char hex[96];
    vigil_sha384(data, len, hash);
    bytes_to_hex(hash, 48, hex);
    return push_bytes_and_ok(vm, (uint8_t *)hex, 96, error);
}

/* ── crypto.sha512(data) -> (string, err) ────────────────────────── */

static vigil_status_t crypto_sha512(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *data;
    size_t len;
    if (!get_bytes_arg(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "sha512: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    uint8_t hash[64];
    char hex[128];
    vigil_sha512(data, len, hash);
    bytes_to_hex(hash, 64, hex);
    return push_bytes_and_ok(vm, (uint8_t *)hex, 128, error);
}

/* ── crypto.hmac_sha256(key, data) -> (string, err) ──────────────── */

static vigil_status_t crypto_hmac_sha256(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *key, *data;
    size_t key_len, data_len;
    if (!get_bytes_arg(vm, base, 0, &key, &key_len) || !get_bytes_arg(vm, base, 1, &data, &data_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "hmac_sha256: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    uint8_t mac[32];
    char hex[64];
    vigil_hmac_sha256(key, key_len, data, data_len, mac);
    bytes_to_hex(mac, 32, hex);
    return push_bytes_and_ok(vm, (uint8_t *)hex, 64, error);
}

/* ── crypto.hmac_sha384(key, data) -> (string, err) ──────────────── */

static vigil_status_t crypto_hmac_sha384(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *key, *data;
    size_t key_len, data_len;
    if (!get_bytes_arg(vm, base, 0, &key, &key_len) || !get_bytes_arg(vm, base, 1, &data, &data_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "hmac_sha384: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    uint8_t mac[48];
    char hex[96];
    vigil_hmac_sha384(key, key_len, data, data_len, mac);
    bytes_to_hex(mac, 48, hex);
    return push_bytes_and_ok(vm, (uint8_t *)hex, 96, error);
}

/* ── crypto.hmac_sha512(key, data) -> (string, err) ──────────────── */

static vigil_status_t crypto_hmac_sha512(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *key, *data;
    size_t key_len, data_len;
    if (!get_bytes_arg(vm, base, 0, &key, &key_len) || !get_bytes_arg(vm, base, 1, &data, &data_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "hmac_sha512: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);
    uint8_t mac[64];
    char hex[128];
    vigil_hmac_sha512(key, key_len, data, data_len, mac);
    bytes_to_hex(mac, 64, hex);
    return push_bytes_and_ok(vm, (uint8_t *)hex, 128, error);
}

/* ── crypto.pbkdf2(password, salt, iterations, key_len) -> (string, err) ── */

static vigil_status_t crypto_pbkdf2(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *password, *salt;
    size_t pass_len, salt_len;
    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &password, &pass_len) || !get_bytes_arg(vm, base, 1, &salt, &salt_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "pbkdf2: invalid argument", error);
    }
    int32_t iterations = get_i32_arg(vm, base, 2);
    int32_t key_len = get_i32_arg(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (iterations < 1 || key_len < 1 || key_len > 1024)
        return push_bytes_and_err(vm, "pbkdf2: invalid iterations or key_len", error);

    uint8_t *key = (uint8_t *)alloc.allocate(alloc.user_data, (size_t)key_len);
    if (!key)
        return push_bytes_and_err(vm, "pbkdf2: allocation failed", error);

    vigil_pbkdf2_sha256(password, pass_len, salt, salt_len, (uint32_t)iterations, key, (size_t)key_len);

    char *hex = (char *)alloc.allocate(alloc.user_data, (size_t)key_len * 2);
    if (!hex)
    {
        alloc.deallocate(alloc.user_data, key);
        return push_bytes_and_err(vm, "pbkdf2: allocation failed", error);
    }
    bytes_to_hex(key, (size_t)key_len, hex);
    vigil_status_t s = push_bytes_and_ok(vm, (uint8_t *)hex, (size_t)key_len * 2, error);
    alloc.deallocate(alloc.user_data, key);
    alloc.deallocate(alloc.user_data, hex);
    return s;
}

/* ── crypto.hkdf_sha256(ikm, salt, info, length) -> (string, err) ── */

static vigil_status_t crypto_hkdf_sha256(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *ikm, *salt, *info;
    size_t ikm_len, salt_len, info_len;
    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &ikm, &ikm_len) || !get_bytes_arg(vm, base, 1, &salt, &salt_len) ||
        !get_bytes_arg(vm, base, 2, &info, &info_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "hkdf_sha256: invalid argument", error);
    }
    int32_t length = get_i32_arg(vm, base, 3);
    vigil_vm_stack_pop_n(vm, arg_count);

    if (length < 1 || length > 8160)
        return push_bytes_and_err(vm, "hkdf_sha256: invalid length", error);

    uint8_t *out = (uint8_t *)alloc.allocate(alloc.user_data, (size_t)length);
    if (!out)
        return push_bytes_and_err(vm, "hkdf_sha256: allocation failed", error);

    vigil_hkdf_sha256(ikm, ikm_len, salt, salt_len, info, info_len, out, (size_t)length);

    char *hex = (char *)alloc.allocate(alloc.user_data, (size_t)length * 2);
    if (!hex)
    {
        alloc.deallocate(alloc.user_data, out);
        return push_bytes_and_err(vm, "hkdf_sha256: allocation failed", error);
    }
    bytes_to_hex(out, (size_t)length, hex);
    vigil_status_t s = push_bytes_and_ok(vm, (uint8_t *)hex, (size_t)length * 2, error);
    alloc.deallocate(alloc.user_data, out);
    alloc.deallocate(alloc.user_data, hex);
    return s;
}

/* ── crypto.encrypt(key, nonce, plaintext, aad) -> (string, err) ── */

static vigil_status_t crypto_encrypt(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *key, *nonce, *plaintext, *aad;
    size_t key_len, nonce_len, pt_len, aad_len;
    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &key, &key_len) || !get_bytes_arg(vm, base, 1, &nonce, &nonce_len) ||
        !get_bytes_arg(vm, base, 2, &plaintext, &pt_len) || !get_bytes_arg(vm, base, 3, &aad, &aad_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "encrypt: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (key_len != 32)
        return push_bytes_and_err(vm, "encrypt: key must be 32 bytes", error);

    size_t out_len = nonce_len + pt_len + 16;
    uint8_t *out = (uint8_t *)alloc.allocate(alloc.user_data, out_len);
    if (!out)
        return push_bytes_and_err(vm, "encrypt: allocation failed", error);

    memcpy(out, nonce, nonce_len);
    vigil_aes256_gcm_encrypt(key, nonce, nonce_len, plaintext, pt_len, aad, aad_len, out + nonce_len,
                             out + nonce_len + pt_len);

    vigil_status_t s = push_bytes_and_ok(vm, out, out_len, error);
    alloc.deallocate(alloc.user_data, out);
    return s;
}

/* ── crypto.decrypt(key, ciphertext, aad) -> (string, err) ───────── */

static vigil_status_t crypto_decrypt(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *key, *ciphertext, *aad;
    size_t key_len, ct_len, aad_len;
    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &key, &key_len) || !get_bytes_arg(vm, base, 1, &ciphertext, &ct_len) ||
        !get_bytes_arg(vm, base, 2, &aad, &aad_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "decrypt: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (key_len != 32 || ct_len < 12 + 16)
        return push_bytes_and_err(vm, "decrypt: invalid key or ciphertext", error);

    const uint8_t *nonce = ciphertext;
    size_t nonce_len = 12;
    const uint8_t *ct = ciphertext + 12;
    size_t pt_len = ct_len - 12 - 16;
    const uint8_t *tag = ciphertext + ct_len - 16;

    uint8_t *plaintext = (uint8_t *)alloc.allocate(alloc.user_data, pt_len > 0 ? pt_len : 1);
    if (!plaintext)
        return push_bytes_and_err(vm, "decrypt: allocation failed", error);

    int result = vigil_aes256_gcm_decrypt(key, nonce, nonce_len, ct, pt_len, aad, aad_len, tag, plaintext);
    if (result != 0)
    {
        alloc.deallocate(alloc.user_data, plaintext);
        return push_bytes_and_err(vm, "decrypt: authentication failed", error);
    }

    vigil_status_t s = push_bytes_and_ok(vm, plaintext, pt_len, error);
    alloc.deallocate(alloc.user_data, plaintext);
    return s;
}

/* ── crypto.aes128_gcm_encrypt(key, nonce, plaintext, aad) -> (string, err) ── */

static vigil_status_t crypto_aes128_gcm_encrypt(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *key, *nonce, *plaintext, *aad;
    size_t key_len, nonce_len, pt_len, aad_len;
    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &key, &key_len) || !get_bytes_arg(vm, base, 1, &nonce, &nonce_len) ||
        !get_bytes_arg(vm, base, 2, &plaintext, &pt_len) || !get_bytes_arg(vm, base, 3, &aad, &aad_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "aes128_gcm_encrypt: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (key_len != 16)
        return push_bytes_and_err(vm, "aes128_gcm_encrypt: key must be 16 bytes", error);

    size_t out_len = nonce_len + pt_len + 16;
    uint8_t *out = (uint8_t *)alloc.allocate(alloc.user_data, out_len);
    if (!out)
        return push_bytes_and_err(vm, "aes128_gcm_encrypt: allocation failed", error);

    memcpy(out, nonce, nonce_len);
    vigil_aes128_gcm_encrypt(key, nonce, nonce_len, plaintext, pt_len, aad, aad_len, out + nonce_len,
                             out + nonce_len + pt_len);

    vigil_status_t s = push_bytes_and_ok(vm, out, out_len, error);
    alloc.deallocate(alloc.user_data, out);
    return s;
}

/* ── crypto.aes128_gcm_decrypt(key, ciphertext, aad) -> (string, err) ── */

static vigil_status_t crypto_aes128_gcm_decrypt(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *key, *ciphertext, *aad;
    size_t key_len, ct_len, aad_len;
    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &key, &key_len) || !get_bytes_arg(vm, base, 1, &ciphertext, &ct_len) ||
        !get_bytes_arg(vm, base, 2, &aad, &aad_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "aes128_gcm_decrypt: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (key_len != 16 || ct_len < 12 + 16)
        return push_bytes_and_err(vm, "aes128_gcm_decrypt: invalid key or ciphertext", error);

    const uint8_t *nonce = ciphertext;
    size_t nonce_len = 12;
    const uint8_t *ct = ciphertext + 12;
    size_t pt_len = ct_len - 12 - 16;
    const uint8_t *tag = ciphertext + ct_len - 16;

    uint8_t *plaintext = (uint8_t *)alloc.allocate(alloc.user_data, pt_len > 0 ? pt_len : 1);
    if (!plaintext)
        return push_bytes_and_err(vm, "aes128_gcm_decrypt: allocation failed", error);

    if (vigil_aes128_gcm_decrypt(key, nonce, nonce_len, ct, pt_len, aad, aad_len, tag, plaintext) != 0)
    {
        alloc.deallocate(alloc.user_data, plaintext);
        return push_bytes_and_err(vm, "aes128_gcm_decrypt: authentication failed", error);
    }

    vigil_status_t s = push_bytes_and_ok(vm, plaintext, pt_len, error);
    alloc.deallocate(alloc.user_data, plaintext);
    return s;
}

/* ── crypto.chacha20_poly1305_encrypt(key, nonce, plaintext, aad) -> (string, err) ── */

static vigil_status_t crypto_chacha20_poly1305_encrypt(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *key, *nonce, *plaintext, *aad;
    size_t key_len, nonce_len, pt_len, aad_len;
    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &key, &key_len) || !get_bytes_arg(vm, base, 1, &nonce, &nonce_len) ||
        !get_bytes_arg(vm, base, 2, &plaintext, &pt_len) || !get_bytes_arg(vm, base, 3, &aad, &aad_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "chacha20_poly1305_encrypt: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (key_len != 32 || nonce_len != 12)
        return push_bytes_and_err(vm, "chacha20_poly1305_encrypt: key must be 32 bytes, nonce 12", error);

    size_t out_len = nonce_len + pt_len + 16;
    uint8_t *out = (uint8_t *)alloc.allocate(alloc.user_data, out_len);
    if (!out)
        return push_bytes_and_err(vm, "chacha20_poly1305_encrypt: allocation failed", error);

    memcpy(out, nonce, 12);
    vigil_chacha20_poly1305_encrypt(key, nonce, plaintext, pt_len, aad, aad_len, out + 12, out + 12 + pt_len);

    vigil_status_t s = push_bytes_and_ok(vm, out, out_len, error);
    alloc.deallocate(alloc.user_data, out);
    return s;
}

/* ── crypto.chacha20_poly1305_decrypt(key, ciphertext, aad) -> (string, err) ── */

static vigil_status_t crypto_chacha20_poly1305_decrypt(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *key, *ciphertext, *aad;
    size_t key_len, ct_len, aad_len;
    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &key, &key_len) || !get_bytes_arg(vm, base, 1, &ciphertext, &ct_len) ||
        !get_bytes_arg(vm, base, 2, &aad, &aad_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "chacha20_poly1305_decrypt: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (key_len != 32 || ct_len < 12 + 16)
        return push_bytes_and_err(vm, "chacha20_poly1305_decrypt: invalid key or ciphertext", error);

    const uint8_t *nonce = ciphertext;
    const uint8_t *ct = ciphertext + 12;
    size_t pt_len = ct_len - 12 - 16;
    const uint8_t *tag = ciphertext + ct_len - 16;

    uint8_t *plaintext = (uint8_t *)alloc.allocate(alloc.user_data, pt_len > 0 ? pt_len : 1);
    if (!plaintext)
        return push_bytes_and_err(vm, "chacha20_poly1305_decrypt: allocation failed", error);

    if (vigil_chacha20_poly1305_decrypt(key, nonce, ct, pt_len, aad, aad_len, tag, plaintext) != 0)
    {
        alloc.deallocate(alloc.user_data, plaintext);
        return push_bytes_and_err(vm, "chacha20_poly1305_decrypt: authentication failed", error);
    }

    vigil_status_t s = push_bytes_and_ok(vm, plaintext, pt_len, error);
    alloc.deallocate(alloc.user_data, plaintext);
    return s;
}

/* ── crypto.aes_cbc_encrypt(key, iv, plaintext) -> (string, err) ── */

static vigil_status_t crypto_aes_cbc_encrypt(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *key, *iv, *plaintext;
    size_t key_len, iv_len, pt_len;
    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &key, &key_len) || !get_bytes_arg(vm, base, 1, &iv, &iv_len) ||
        !get_bytes_arg(vm, base, 2, &plaintext, &pt_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "aes_cbc_encrypt: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (key_len != 32 || iv_len != 16)
        return push_bytes_and_err(vm, "aes_cbc_encrypt: key must be 32 bytes, iv 16", error);

    size_t out_max = pt_len + 16;
    uint8_t *out = (uint8_t *)alloc.allocate(alloc.user_data, out_max);
    if (!out)
        return push_bytes_and_err(vm, "aes_cbc_encrypt: allocation failed", error);

    size_t out_len = vigil_aes_cbc_encrypt(key, key_len, iv, plaintext, pt_len, out);
    if (out_len == 0)
    {
        alloc.deallocate(alloc.user_data, out);
        return push_bytes_and_err(vm, "aes_cbc_encrypt: encryption failed", error);
    }

    vigil_status_t s = push_bytes_and_ok(vm, out, out_len, error);
    alloc.deallocate(alloc.user_data, out);
    return s;
}

/* ── crypto.aes_cbc_decrypt(key, iv, ciphertext) -> (string, err) ── */

static vigil_status_t crypto_aes_cbc_decrypt(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *key, *iv, *ciphertext;
    size_t key_len, iv_len, ct_len;
    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &key, &key_len) || !get_bytes_arg(vm, base, 1, &iv, &iv_len) ||
        !get_bytes_arg(vm, base, 2, &ciphertext, &ct_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "aes_cbc_decrypt: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (key_len != 32 || iv_len != 16 || ct_len == 0 || ct_len % 16 != 0)
        return push_bytes_and_err(vm, "aes_cbc_decrypt: invalid key, iv, or ciphertext", error);

    uint8_t *out = (uint8_t *)alloc.allocate(alloc.user_data, ct_len);
    if (!out)
        return push_bytes_and_err(vm, "aes_cbc_decrypt: allocation failed", error);

    size_t pt_len = vigil_aes_cbc_decrypt(key, key_len, iv, ciphertext, ct_len, out);
    if (pt_len == 0)
    {
        alloc.deallocate(alloc.user_data, out);
        return push_bytes_and_err(vm, "aes_cbc_decrypt: decryption failed", error);
    }

    vigil_status_t s = push_bytes_and_ok(vm, out, pt_len, error);
    alloc.deallocate(alloc.user_data, out);
    return s;
}

/* ── crypto.password_encrypt(password, plaintext) -> (string, err) ── */

static vigil_status_t crypto_password_encrypt(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *password, *plaintext;
    size_t pass_len, pt_len;
    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &password, &pass_len) || !get_bytes_arg(vm, base, 1, &plaintext, &pt_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "password_encrypt: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    uint8_t *out = (uint8_t *)alloc.allocate(alloc.user_data, pt_len + 44);
    if (!out)
        return push_bytes_and_err(vm, "password_encrypt: allocation failed", error);

    size_t out_len = vigil_password_encrypt(password, pass_len, plaintext, pt_len, out);
    if (out_len == 0)
    {
        alloc.deallocate(alloc.user_data, out);
        return push_bytes_and_err(vm, "password_encrypt: encryption failed", error);
    }

    vigil_status_t s = push_bytes_and_ok(vm, out, out_len, error);
    alloc.deallocate(alloc.user_data, out);
    return s;
}

/* ── crypto.password_decrypt(password, ciphertext) -> (string, err) ── */

static vigil_status_t crypto_password_decrypt(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *password, *ciphertext;
    size_t pass_len, ct_len;
    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &password, &pass_len) || !get_bytes_arg(vm, base, 1, &ciphertext, &ct_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "password_decrypt: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (ct_len < 44)
        return push_bytes_and_err(vm, "password_decrypt: ciphertext too short", error);

    uint8_t *out = (uint8_t *)alloc.allocate(alloc.user_data, ct_len);
    if (!out)
        return push_bytes_and_err(vm, "password_decrypt: allocation failed", error);

    size_t pt_len = vigil_password_decrypt(password, pass_len, ciphertext, ct_len, out);
    if (pt_len == 0)
    {
        alloc.deallocate(alloc.user_data, out);
        return push_bytes_and_err(vm, "password_decrypt: decryption failed", error);
    }

    vigil_status_t s = push_bytes_and_ok(vm, out, pt_len, error);
    alloc.deallocate(alloc.user_data, out);
    return s;
}

/* ── crypto.x25519_keypair() -> (string, err) ────────────────────── */

static vigil_status_t crypto_x25519_keypair(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    uint8_t priv[32], pub[32];
    char hex[128];
    vigil_x25519_keypair(pub, priv);
    bytes_to_hex(priv, 32, hex);
    bytes_to_hex(pub, 32, hex + 64);
    return push_bytes_and_ok(vm, (uint8_t *)hex, 128, error);
}

/* ── crypto.x25519(private_key_hex, public_key_hex) -> (string, err) ── */

static vigil_status_t crypto_x25519(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *priv_hex, *pub_hex;
    size_t priv_len, pub_len;

    if (!get_bytes_arg(vm, base, 0, &priv_hex, &priv_len) || !get_bytes_arg(vm, base, 1, &pub_hex, &pub_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "x25519: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (priv_len != 64 || pub_len != 64)
        return push_bytes_and_err(vm, "x25519: keys must be 64 hex chars (32 bytes)", error);

    uint8_t priv[32], pub[32], shared[32];
    if (hex_to_bytes((const char *)priv_hex, 64, priv) != 0 ||
        hex_to_bytes((const char *)pub_hex, 64, pub) != 0)
        return push_bytes_and_err(vm, "x25519: invalid hex", error);

    vigil_x25519(shared, priv, pub);
    char hex[64];
    bytes_to_hex(shared, 32, hex);
    return push_bytes_and_ok(vm, (uint8_t *)hex, 64, error);
}

/* ── crypto.ed25519_keypair() -> (string, err) ───────────────────── */

static vigil_status_t crypto_ed25519_keypair(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    vigil_vm_stack_pop_n(vm, arg_count);
    uint8_t priv[64], pub[32];
    char hex[192];
    vigil_ed25519_keypair(pub, priv);
    bytes_to_hex(priv, 64, hex);
    bytes_to_hex(pub, 32, hex + 128);
    return push_bytes_and_ok(vm, (uint8_t *)hex, 192, error);
}

/* ── crypto.ed25519_sign(private_key_hex, message) -> (string, err) ── */

static vigil_status_t crypto_ed25519_sign(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *priv_hex, *message;
    size_t priv_len, msg_len;

    if (!get_bytes_arg(vm, base, 0, &priv_hex, &priv_len) || !get_bytes_arg(vm, base, 1, &message, &msg_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "ed25519_sign: invalid argument", error);
    }

    if (priv_len != 128)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "ed25519_sign: private key must be 128 hex chars (64 bytes)", error);
    }

    uint8_t priv[64], sig[64];
    if (hex_to_bytes((const char *)priv_hex, 128, priv) != 0)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "ed25519_sign: invalid hex", error);
    }

    /* Copy message before popping — stack pop may release the backing string */
    vigil_allocator_t alloc = get_alloc(vm);
    uint8_t *msg_copy = (uint8_t *)alloc.allocate(alloc.user_data, msg_len + 1);
    if (!msg_copy)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "ed25519_sign: out of memory", error);
    }
    memcpy(msg_copy, message, msg_len);

    vigil_vm_stack_pop_n(vm, arg_count);

    vigil_ed25519_sign(sig, msg_copy, msg_len, priv);
    alloc.deallocate(alloc.user_data, msg_copy);

    char hex[128];
    bytes_to_hex(sig, 64, hex);
    return push_bytes_and_ok(vm, (const uint8_t *)hex, 128, error);
}

/* ── crypto.ed25519_verify(public_key_hex, message, signature_hex) -> bool ── */

static vigil_status_t crypto_ed25519_verify(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *pub_hex, *message, *sig_hex;
    size_t pub_len, msg_len, sig_len;

    if (!get_bytes_arg(vm, base, 0, &pub_hex, &pub_len) || !get_bytes_arg(vm, base, 1, &message, &msg_len) ||
        !get_bytes_arg(vm, base, 2, &sig_hex, &sig_len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    if (pub_len != 64 || sig_len != 128)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    uint8_t pub[32], sig[64];
    if (hex_to_bytes((const char *)pub_hex, 64, pub) != 0 ||
        hex_to_bytes((const char *)sig_hex, 128, sig) != 0)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }

    /* Copy message before popping — stack pop may release the backing string object */
    vigil_allocator_t alloc = get_alloc(vm);
    uint8_t *msg_copy = (uint8_t *)alloc.allocate(alloc.user_data, msg_len + 1);
    if (!msg_copy)
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bool(vm, 0, error);
    }
    memcpy(msg_copy, message, msg_len);

    vigil_vm_stack_pop_n(vm, arg_count);

    int result = vigil_ed25519_verify(sig, msg_copy, msg_len, pub);
    alloc.deallocate(alloc.user_data, msg_copy);
    return push_bool(vm, result == 0, error);
}

/* ── crypto.random_bytes(len) -> (string, err) ───────────────────── */

static vigil_status_t crypto_random_bytes(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    int32_t len = get_i32_arg(vm, base, 0);
    vigil_vm_stack_pop_n(vm, arg_count);
    vigil_allocator_t alloc = get_alloc(vm);

    if (len < 0 || len > 65536)
        return push_bytes_and_err(vm, "random_bytes: length must be 0-65536", error);

    uint8_t *buf = (uint8_t *)alloc.allocate(alloc.user_data, (size_t)len);
    if (!buf)
        return push_bytes_and_err(vm, "random_bytes: allocation failed", error);

    vigil_crypto_random_bytes(buf, (size_t)len);
    vigil_status_t s = push_bytes_and_ok(vm, buf, (size_t)len, error);
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

/* ── crypto.hex_encode(data) -> (string, err) ────────────────────── */

static vigil_status_t crypto_hex_encode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *data;
    size_t len;
    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "hex_encode: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    char *hex = (char *)alloc.allocate(alloc.user_data, len * 2);
    if (!hex)
        return push_bytes_and_err(vm, "hex_encode: allocation failed", error);
    bytes_to_hex(data, len, hex);
    vigil_status_t s = push_bytes_and_ok(vm, (uint8_t *)hex, len * 2, error);
    alloc.deallocate(alloc.user_data, hex);
    return s;
}

/* ── crypto.hex_decode(hex) -> (string, err) ─────────────────────── */

static vigil_status_t crypto_hex_decode(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *hex;
    size_t len;
    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &hex, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "hex_decode: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    if (len % 2 != 0)
        return push_bytes_and_err(vm, "hex_decode: odd-length input", error);

    uint8_t *out = (uint8_t *)alloc.allocate(alloc.user_data, len / 2);
    if (!out)
        return push_bytes_and_err(vm, "hex_decode: allocation failed", error);
    if (hex_to_bytes((const char *)hex, len, out) != 0)
    {
        alloc.deallocate(alloc.user_data, out);
        return push_bytes_and_err(vm, "hex_decode: invalid hex", error);
    }
    vigil_status_t s = push_bytes_and_ok(vm, out, len / 2, error);
    alloc.deallocate(alloc.user_data, out);
    return s;
}

/* ── crypto.base64_encode(data) -> (string, err) ─────────────────── */

static vigil_status_t crypto_base64_encode_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *data;
    size_t len;
    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "base64_encode: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    size_t out_len = ((len + 2) / 3) * 4;
    char *out = (char *)alloc.allocate(alloc.user_data, out_len);
    if (!out)
        return push_bytes_and_err(vm, "base64_encode: allocation failed", error);
    size_t actual = base64_encode(data, len, out);
    vigil_status_t s = push_bytes_and_ok(vm, (uint8_t *)out, actual, error);
    alloc.deallocate(alloc.user_data, out);
    return s;
}

/* ── crypto.base64_decode(data) -> (string, err) ─────────────────── */

static vigil_status_t crypto_base64_decode_fn(vigil_vm_t *vm, size_t arg_count, vigil_error_t *error)
{
    size_t base = vigil_vm_stack_depth(vm) - arg_count;
    const uint8_t *data;
    size_t len;
    vigil_allocator_t alloc = get_alloc(vm);

    if (!get_bytes_arg(vm, base, 0, &data, &len))
    {
        vigil_vm_stack_pop_n(vm, arg_count);
        return push_bytes_and_err(vm, "base64_decode: invalid argument", error);
    }
    vigil_vm_stack_pop_n(vm, arg_count);

    size_t out_len = (len / 4) * 3;
    uint8_t *out = (uint8_t *)alloc.allocate(alloc.user_data, out_len > 0 ? out_len : 1);
    if (!out)
        return push_bytes_and_err(vm, "base64_decode: allocation failed", error);
    size_t actual = base64_decode((const char *)data, len, out);
    vigil_status_t s = push_bytes_and_ok(vm, out, actual, error);
    alloc.deallocate(alloc.user_data, out);
    return s;
}

/* ── Module descriptor ───────────────────────────────────────────── */

static const int p_str[] = {VIGIL_TYPE_STRING};
static const int p_2str[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int p_3str[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int p_4str[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING};
static const int p_pbkdf2[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_I32, VIGIL_TYPE_I32};
static const int p_hkdf[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_STRING, VIGIL_TYPE_I32};
static const int p_i32[] = {VIGIL_TYPE_I32};

static const int str_err_returns[] = {VIGIL_TYPE_STRING, VIGIL_TYPE_ERR};

static const char *const pn_data[] = {"data"};
static const char *const pn_key_data[] = {"key", "data"};
static const char *const pn_pbkdf2[] = {"password", "salt", "iterations", "key_len"};
static const char *const pn_hkdf[] = {"ikm", "salt", "info", "length"};
static const char *const pn_encrypt[] = {"key", "nonce", "plaintext", "aad"};
static const char *const pn_decrypt[] = {"key", "ciphertext", "aad"};
static const char *const pn_cbc_enc[] = {"key", "iv", "plaintext"};
static const char *const pn_cbc_dec[] = {"key", "iv", "ciphertext"};
static const char *const pn_pw_enc[] = {"password", "plaintext"};
static const char *const pn_pw_dec[] = {"password", "ciphertext"};
static const char *const pn_x25519[] = {"private_key_hex", "public_key_hex"};
static const char *const pn_ed_sign[] = {"private_key_hex", "message"};
static const char *const pn_ed_verify[] = {"public_key_hex", "message", "signature_hex"};
static const char *const pn_len[] = {"len"};
static const char *const pn_ab[] = {"a", "b"};
static const char *const pn_hex[] = {"hex"};
static const char *const pn_b64[] = {"b64"};

/* ── Doc strings ─────────────────────────────────────────────────── */

static const vigil_native_symbol_doc_t doc_module = {
    "Cryptographic operations.",
    "The crypto module provides secure hashing, encryption, key derivation,\n"
    "key exchange, and digital signatures. All fallible functions return\n"
    "(result, err) multi-return values.",
    NULL,
};

static const vigil_native_symbol_doc_t doc_sha224 = {"SHA-224 hash.", "Returns 56-character hex string.", "crypto.sha224(\"hello\")"};
static const vigil_native_symbol_doc_t doc_sha256 = {"SHA-256 hash.", "Returns 64-character hex string.", "crypto.sha256(\"hello\")"};
static const vigil_native_symbol_doc_t doc_sha384 = {"SHA-384 hash.", "Returns 96-character hex string.", "crypto.sha384(\"hello\")"};
static const vigil_native_symbol_doc_t doc_sha512 = {"SHA-512 hash.", "Returns 128-character hex string.", "crypto.sha512(\"hello\")"};

static const vigil_native_symbol_doc_t doc_hmac_sha256 = {"HMAC-SHA256.", "Returns 64-character hex string.", "crypto.hmac_sha256(\"key\", \"message\")"};
static const vigil_native_symbol_doc_t doc_hmac_sha384 = {"HMAC-SHA384.", "Returns 96-character hex string.", "crypto.hmac_sha384(\"key\", \"message\")"};
static const vigil_native_symbol_doc_t doc_hmac_sha512 = {"HMAC-SHA512.", "Returns 128-character hex string.", "crypto.hmac_sha512(\"key\", \"message\")"};

static const vigil_native_symbol_doc_t doc_pbkdf2 = {"PBKDF2 key derivation.", "Returns hex-encoded derived key. Use 100000+ iterations.", "crypto.pbkdf2(\"password\", \"salt\", 100000, 32)"};
static const vigil_native_symbol_doc_t doc_hkdf_sha256 = {"HKDF-SHA256 key derivation.", "Returns hex-encoded derived key material.", "crypto.hkdf_sha256(ikm, salt, info, 32)"};

static const vigil_native_symbol_doc_t doc_encrypt = {"AES-256-GCM encryption.", "Key must be 32 bytes. Returns nonce||ciphertext||tag.", "crypto.encrypt(key, nonce, \"secret\", \"\")"};
static const vigil_native_symbol_doc_t doc_decrypt = {"AES-256-GCM decryption.", "Returns error on authentication failure.", "crypto.decrypt(key, encrypted, \"\")"};
static const vigil_native_symbol_doc_t doc_aes128_gcm_encrypt = {"AES-128-GCM encryption.", "Key must be 16 bytes. Returns nonce||ciphertext||tag.", "crypto.aes128_gcm_encrypt(key, nonce, \"secret\", \"\")"};
static const vigil_native_symbol_doc_t doc_aes128_gcm_decrypt = {"AES-128-GCM decryption.", "Returns error on authentication failure.", "crypto.aes128_gcm_decrypt(key, encrypted, \"\")"};
static const vigil_native_symbol_doc_t doc_chacha20_encrypt = {"ChaCha20-Poly1305 encryption.", "Key 32 bytes, nonce 12 bytes.", "crypto.chacha20_poly1305_encrypt(key, nonce, \"secret\", \"\")"};
static const vigil_native_symbol_doc_t doc_chacha20_decrypt = {"ChaCha20-Poly1305 decryption.", "Returns error on authentication failure.", "crypto.chacha20_poly1305_decrypt(key, encrypted, \"\")"};
static const vigil_native_symbol_doc_t doc_aes_cbc_encrypt = {"AES-CBC encryption with PKCS#7 padding.", "Key 32 bytes, IV 16 bytes.", "crypto.aes_cbc_encrypt(key, iv, \"secret\")"};
static const vigil_native_symbol_doc_t doc_aes_cbc_decrypt = {"AES-CBC decryption with PKCS#7 unpadding.", "Key 32 bytes, IV 16 bytes.", "crypto.aes_cbc_decrypt(key, iv, ciphertext)"};

static const vigil_native_symbol_doc_t doc_pw_encrypt = {"Password-based encryption.", "Uses PBKDF2 + AES-256-GCM.", "crypto.password_encrypt(\"my password\", \"secret\")"};
static const vigil_native_symbol_doc_t doc_pw_decrypt = {"Password-based decryption.", "Returns error on wrong password.", "crypto.password_decrypt(\"my password\", encrypted)"};

static const vigil_native_symbol_doc_t doc_x25519_keypair = {"Generate X25519 keypair.", "Returns hex(private 32) + hex(public 32) = 128 chars.", "crypto.x25519_keypair()"};
static const vigil_native_symbol_doc_t doc_x25519 = {"X25519 key exchange.", "Returns hex shared secret (64 chars).", "crypto.x25519(priv_hex, pub_hex)"};
static const vigil_native_symbol_doc_t doc_ed25519_keypair = {"Generate Ed25519 keypair.", "Returns hex(private 64) + hex(public 32) = 192 chars.", "crypto.ed25519_keypair()"};
static const vigil_native_symbol_doc_t doc_ed25519_sign = {"Ed25519 signature.", "Returns hex signature (128 chars).", "crypto.ed25519_sign(priv_hex, message)"};
static const vigil_native_symbol_doc_t doc_ed25519_verify = {"Ed25519 verification.", "Returns true if signature is valid.", "crypto.ed25519_verify(pub_hex, message, sig_hex)"};

static const vigil_native_symbol_doc_t doc_random_bytes = {"Cryptographically secure random bytes.", "Returns raw bytes. Max 65536.", "crypto.random_bytes(32)"};
static const vigil_native_symbol_doc_t doc_constant_time_eq = {"Constant-time comparison.", "Prevents timing attacks.", "crypto.constant_time_eq(hash1, hash2)"};
static const vigil_native_symbol_doc_t doc_hex_encode = {"Encode bytes as hex.", "Returns lowercase hex string.", "crypto.hex_encode(data)"};
static const vigil_native_symbol_doc_t doc_hex_decode = {"Decode hex to bytes.", "Returns error on invalid input.", "crypto.hex_decode(\"00ff\")"};
static const vigil_native_symbol_doc_t doc_base64_encode = {"Encode bytes as base64.", "Standard base64 with padding.", "crypto.base64_encode(\"hello\")"};
static const vigil_native_symbol_doc_t doc_base64_decode = {"Decode base64 to bytes.", "Returns error on invalid input.", "crypto.base64_decode(\"aGVsbG8=\")"};

/* ── Function table ──────────────────────────────────────────────── */

static const vigil_native_module_function_t crypto_functions[] = {
    /* Hashes */
    {"sha224", 6, crypto_sha224, 1, p_str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_data, NULL, NULL, &doc_sha224},
    {"sha256", 6, crypto_sha256, 1, p_str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_data, NULL, NULL, &doc_sha256},
    {"sha384", 6, crypto_sha384, 1, p_str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_data, NULL, NULL, &doc_sha384},
    {"sha512", 6, crypto_sha512, 1, p_str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_data, NULL, NULL, &doc_sha512},

    /* MACs */
    {"hmac_sha256", 11, crypto_hmac_sha256, 2, p_2str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_key_data, NULL, NULL, &doc_hmac_sha256},
    {"hmac_sha384", 11, crypto_hmac_sha384, 2, p_2str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_key_data, NULL, NULL, &doc_hmac_sha384},
    {"hmac_sha512", 11, crypto_hmac_sha512, 2, p_2str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_key_data, NULL, NULL, &doc_hmac_sha512},

    /* KDFs */
    {"pbkdf2", 6, crypto_pbkdf2, 4, p_pbkdf2, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_pbkdf2, NULL, NULL, &doc_pbkdf2},
    {"hkdf_sha256", 11, crypto_hkdf_sha256, 4, p_hkdf, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_hkdf, NULL, NULL, &doc_hkdf_sha256},

    /* Symmetric encryption */
    {"encrypt", 7, crypto_encrypt, 4, p_4str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_encrypt, NULL, NULL, &doc_encrypt},
    {"decrypt", 7, crypto_decrypt, 3, p_3str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_decrypt, NULL, NULL, &doc_decrypt},
    {"aes128_gcm_encrypt", 18, crypto_aes128_gcm_encrypt, 4, p_4str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_encrypt, NULL, NULL, &doc_aes128_gcm_encrypt},
    {"aes128_gcm_decrypt", 18, crypto_aes128_gcm_decrypt, 3, p_3str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_decrypt, NULL, NULL, &doc_aes128_gcm_decrypt},
    {"chacha20_poly1305_encrypt", 25, crypto_chacha20_poly1305_encrypt, 4, p_4str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_encrypt, NULL, NULL, &doc_chacha20_encrypt},
    {"chacha20_poly1305_decrypt", 25, crypto_chacha20_poly1305_decrypt, 3, p_3str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_decrypt, NULL, NULL, &doc_chacha20_decrypt},
    {"aes_cbc_encrypt", 15, crypto_aes_cbc_encrypt, 3, p_3str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_cbc_enc, NULL, NULL, &doc_aes_cbc_encrypt},
    {"aes_cbc_decrypt", 15, crypto_aes_cbc_decrypt, 3, p_3str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_cbc_dec, NULL, NULL, &doc_aes_cbc_decrypt},

    /* Password-based */
    {"password_encrypt", 16, crypto_password_encrypt, 2, p_2str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_pw_enc, NULL, NULL, &doc_pw_encrypt},
    {"password_decrypt", 16, crypto_password_decrypt, 2, p_2str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_pw_dec, NULL, NULL, &doc_pw_decrypt},

    /* Key exchange */
    {"x25519_keypair", 14, crypto_x25519_keypair, 0, NULL, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, &doc_x25519_keypair},
    {"x25519", 6, crypto_x25519, 2, p_2str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_x25519, NULL, NULL, &doc_x25519},

    /* Signatures */
    {"ed25519_keypair", 15, crypto_ed25519_keypair, 0, NULL, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     NULL, NULL, NULL, &doc_ed25519_keypair},
    {"ed25519_sign", 12, crypto_ed25519_sign, 2, p_2str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_ed_sign, NULL, NULL, &doc_ed25519_sign},
    {"ed25519_verify", 14, crypto_ed25519_verify, 3, p_3str, VIGIL_TYPE_BOOL, 1, NULL, 0, NULL, NULL, 0U,
     pn_ed_verify, NULL, NULL, &doc_ed25519_verify},

    /* Utilities */
    {"random_bytes", 12, crypto_random_bytes, 1, p_i32, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_len, NULL, NULL, &doc_random_bytes},
    {"constant_time_eq", 16, crypto_constant_time_eq, 2, p_2str, VIGIL_TYPE_BOOL, 1, NULL, 0, NULL, NULL, 0U,
     pn_ab, NULL, NULL, &doc_constant_time_eq},
    {"hex_encode", 10, crypto_hex_encode, 1, p_str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_data, NULL, NULL, &doc_hex_encode},
    {"hex_decode", 10, crypto_hex_decode, 1, p_str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_hex, NULL, NULL, &doc_hex_decode},
    {"base64_encode", 13, crypto_base64_encode_fn, 1, p_str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_data, NULL, NULL, &doc_base64_encode},
    {"base64_decode", 13, crypto_base64_decode_fn, 1, p_str, VIGIL_TYPE_STRING, 2, str_err_returns, 0, NULL, NULL, 0U,
     pn_b64, NULL, NULL, &doc_base64_decode},
};

VIGIL_API const vigil_native_module_t vigil_stdlib_crypto = {
    "crypto", 6, crypto_functions, sizeof(crypto_functions) / sizeof(crypto_functions[0]),
    NULL, 0, &doc_module, NULL, 0U};
