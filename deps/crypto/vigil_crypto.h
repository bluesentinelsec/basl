/* Minimal portable crypto library for VIGIL.
 * Public domain / CC0 implementations.
 */
#ifndef VIGIL_CRYPTO_H
#define VIGIL_CRYPTO_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ── SHA-256 ─────────────────────────────────────────────────────── */

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
} vigil_sha256_ctx;

void vigil_sha256_init(vigil_sha256_ctx *ctx);
void vigil_sha256_update(vigil_sha256_ctx *ctx, const uint8_t *data, size_t len);
void vigil_sha256_final(vigil_sha256_ctx *ctx, uint8_t out[32]);
void vigil_sha256(const uint8_t *data, size_t len, uint8_t out[32]);

/* ── SHA-512 ─────────────────────────────────────────────────────── */

typedef struct {
    uint64_t state[8];
    uint64_t count[2];
    uint8_t buffer[128];
} vigil_sha512_ctx;

void vigil_sha512_init(vigil_sha512_ctx *ctx);
void vigil_sha512_update(vigil_sha512_ctx *ctx, const uint8_t *data, size_t len);
void vigil_sha512_final(vigil_sha512_ctx *ctx, uint8_t out[64]);
void vigil_sha512(const uint8_t *data, size_t len, uint8_t out[64]);
void vigil_sha384(const uint8_t *data, size_t len, uint8_t out[48]);

/* ── HMAC ────────────────────────────────────────────────────────── */

void vigil_hmac_sha256(const uint8_t *key, size_t key_len,
                      const uint8_t *data, size_t data_len,
                      uint8_t out[32]);

/* ── AES-256-GCM ─────────────────────────────────────────────────── */

int vigil_aes256_gcm_encrypt(const uint8_t key[32], const uint8_t *nonce, size_t nonce_len,
                            const uint8_t *plaintext, size_t pt_len,
                            const uint8_t *aad, size_t aad_len,
                            uint8_t *ciphertext, uint8_t tag[16]);

int vigil_aes256_gcm_decrypt(const uint8_t key[32], const uint8_t *nonce, size_t nonce_len,
                            const uint8_t *ciphertext, size_t ct_len,
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t tag[16], uint8_t *plaintext);

/* ── PBKDF2 ──────────────────────────────────────────────────────── */

void vigil_pbkdf2_sha256(const uint8_t *password, size_t pass_len,
                        const uint8_t *salt, size_t salt_len,
                        uint32_t iterations, uint8_t *out, size_t out_len);

/* ── Password-based encryption (PBKDF2 + AES-256-GCM) ────────────── */

/* Encrypts with password. Output: salt(16) || nonce(12) || ciphertext || tag(16)
 * Returns output length, or 0 on failure. out must be at least pt_len + 44 bytes. */
size_t vigil_password_encrypt(const uint8_t *password, size_t pass_len,
                             const uint8_t *plaintext, size_t pt_len,
                             uint8_t *out);

/* Decrypts password-encrypted data. Returns plaintext length, or 0 on failure. */
size_t vigil_password_decrypt(const uint8_t *password, size_t pass_len,
                             const uint8_t *ciphertext, size_t ct_len,
                             uint8_t *out);

/* ── SHA-224 ──────────────────────────────────────────────────────── */

void vigil_sha224(const uint8_t *data, size_t len, uint8_t out[28]);

/* ── HMAC-SHA384 / HMAC-SHA512 ───────────────────────────────────── */

void vigil_hmac_sha384(const uint8_t *key, size_t key_len,
                       const uint8_t *data, size_t data_len,
                       uint8_t out[48]);

void vigil_hmac_sha512(const uint8_t *key, size_t key_len,
                       const uint8_t *data, size_t data_len,
                       uint8_t out[64]);

/* ── HKDF-SHA256 ─────────────────────────────────────────────────── */

void vigil_hkdf_sha256(const uint8_t *ikm, size_t ikm_len,
                       const uint8_t *salt, size_t salt_len,
                       const uint8_t *info, size_t info_len,
                       uint8_t *out, size_t out_len);

/* ── AES-128-GCM ─────────────────────────────────────────────────── */

int vigil_aes128_gcm_encrypt(const uint8_t key[16], const uint8_t *nonce, size_t nonce_len,
                             const uint8_t *plaintext, size_t pt_len,
                             const uint8_t *aad, size_t aad_len,
                             uint8_t *ciphertext, uint8_t tag[16]);

int vigil_aes128_gcm_decrypt(const uint8_t key[16], const uint8_t *nonce, size_t nonce_len,
                             const uint8_t *ciphertext, size_t ct_len,
                             const uint8_t *aad, size_t aad_len,
                             const uint8_t tag[16], uint8_t *plaintext);

/* ── AES-CBC (PKCS#7 padding) ────────────────────────────────────── */

/* Returns ciphertext length (always multiple of 16, includes padding). */
size_t vigil_aes_cbc_encrypt(const uint8_t *key, size_t key_len,
                             const uint8_t iv[16],
                             const uint8_t *plaintext, size_t pt_len,
                             uint8_t *ciphertext);

/* Returns plaintext length after removing padding, or 0 on error. */
size_t vigil_aes_cbc_decrypt(const uint8_t *key, size_t key_len,
                             const uint8_t iv[16],
                             const uint8_t *ciphertext, size_t ct_len,
                             uint8_t *plaintext);

/* ── ChaCha20-Poly1305 ──────────────────────────────────────────── */

int vigil_chacha20_poly1305_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                                    const uint8_t *plaintext, size_t pt_len,
                                    const uint8_t *aad, size_t aad_len,
                                    uint8_t *ciphertext, uint8_t tag[16]);

int vigil_chacha20_poly1305_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                                    const uint8_t *ciphertext, size_t ct_len,
                                    const uint8_t *aad, size_t aad_len,
                                    const uint8_t tag[16], uint8_t *plaintext);

/* ── X25519 Key Exchange ─────────────────────────────────────────── */

void vigil_x25519_keypair(uint8_t public_key[32], uint8_t private_key[32]);
void vigil_x25519(uint8_t shared_secret[32], const uint8_t private_key[32],
                 const uint8_t public_key[32]);

/* ── Ed25519 Signatures ──────────────────────────────────────────── */

void vigil_ed25519_keypair(uint8_t public_key[32], uint8_t private_key[64]);
void vigil_ed25519_sign(uint8_t signature[64], const uint8_t *message, size_t msg_len,
                       const uint8_t private_key[64]);
int vigil_ed25519_verify(const uint8_t signature[64], const uint8_t *message, size_t msg_len,
                        const uint8_t public_key[32]);

/* ── Utilities ───────────────────────────────────────────────────── */

void vigil_crypto_random_bytes(uint8_t *buf, size_t len);
int vigil_crypto_constant_time_compare(const uint8_t *a, const uint8_t *b, size_t len);

#endif /* VIGIL_CRYPTO_H */
