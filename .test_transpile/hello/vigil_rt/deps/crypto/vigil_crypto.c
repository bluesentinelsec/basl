/* Minimal portable crypto library for VIGIL - Implementation.
 * SHA-256/512 based on public domain implementations.
 */
#include "vigil_crypto.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <fcntl.h>
#include <unistd.h>
#endif

/* ── Utilities ───────────────────────────────────────────────────── */

static uint32_t ror32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static uint64_t ror64(uint64_t x, int n) { return (x >> n) | (x << (64 - n)); }

static void store_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void store_be64(uint8_t *p, uint64_t v) {
    store_be32(p, (uint32_t)(v >> 32));
    store_be32(p + 4, (uint32_t)v);
}

static uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t load_be64(const uint8_t *p) {
    return ((uint64_t)load_be32(p) << 32) | load_be32(p + 4);
}

/* ── SHA-256 ─────────────────────────────────────────────────────── */

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

void vigil_sha256_init(vigil_sha256_ctx *ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

static void sha256_transform(vigil_sha256_ctx *ctx, const uint8_t block[64]) {
    uint32_t W[64], a, b, c, d, e, f, g, h, t1, t2;
    int i;

    for (i = 0; i < 16; i++) W[i] = load_be32(block + i * 4);
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ror32(W[i-15], 7) ^ ror32(W[i-15], 18) ^ (W[i-15] >> 3);
        uint32_t s1 = ror32(W[i-2], 17) ^ ror32(W[i-2], 19) ^ (W[i-2] >> 10);
        W[i] = W[i-16] + s0 + W[i-7] + s1;
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        uint32_t S1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        t1 = h + S1 + ch + K256[i] + W[i];
        uint32_t S0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void vigil_sha256_update(vigil_sha256_ctx *ctx, const uint8_t *data, size_t len) {
    size_t i, idx = (size_t)(ctx->count & 63);
    ctx->count += len;
    for (i = 0; i < len; i++) {
        ctx->buffer[idx++] = data[i];
        if (idx == 64) { sha256_transform(ctx, ctx->buffer); idx = 0; }
    }
}

void vigil_sha256_final(vigil_sha256_ctx *ctx, uint8_t out[32]) {
    uint64_t bits = ctx->count * 8;
    size_t idx = (size_t)(ctx->count & 63);
    ctx->buffer[idx++] = 0x80;
    if (idx > 56) {
        while (idx < 64) ctx->buffer[idx++] = 0;
        sha256_transform(ctx, ctx->buffer); idx = 0;
    }
    while (idx < 56) ctx->buffer[idx++] = 0;
    store_be64(ctx->buffer + 56, bits);
    sha256_transform(ctx, ctx->buffer);
    for (int i = 0; i < 8; i++) store_be32(out + i * 4, ctx->state[i]);
}

void vigil_sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
    vigil_sha256_ctx ctx;
    vigil_sha256_init(&ctx);
    vigil_sha256_update(&ctx, data, len);
    vigil_sha256_final(&ctx, out);
}

/* ── SHA-512 ─────────────────────────────────────────────────────── */

static const uint64_t K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

void vigil_sha512_init(vigil_sha512_ctx *ctx) {
    ctx->state[0] = 0x6a09e667f3bcc908ULL; ctx->state[1] = 0xbb67ae8584caa73bULL;
    ctx->state[2] = 0x3c6ef372fe94f82bULL; ctx->state[3] = 0xa54ff53a5f1d36f1ULL;
    ctx->state[4] = 0x510e527fade682d1ULL; ctx->state[5] = 0x9b05688c2b3e6c1fULL;
    ctx->state[6] = 0x1f83d9abfb41bd6bULL; ctx->state[7] = 0x5be0cd19137e2179ULL;
    ctx->count[0] = ctx->count[1] = 0;
}

static void sha512_transform(vigil_sha512_ctx *ctx, const uint8_t block[128]) {
    uint64_t W[80], a, b, c, d, e, f, g, h, t1, t2;
    int i;

    for (i = 0; i < 16; i++) W[i] = load_be64(block + i * 8);
    for (i = 16; i < 80; i++) {
        uint64_t s0 = ror64(W[i-15], 1) ^ ror64(W[i-15], 8) ^ (W[i-15] >> 7);
        uint64_t s1 = ror64(W[i-2], 19) ^ ror64(W[i-2], 61) ^ (W[i-2] >> 6);
        W[i] = W[i-16] + s0 + W[i-7] + s1;
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 80; i++) {
        uint64_t S1 = ror64(e, 14) ^ ror64(e, 18) ^ ror64(e, 41);
        uint64_t ch = (e & f) ^ (~e & g);
        t1 = h + S1 + ch + K512[i] + W[i];
        uint64_t S0 = ror64(a, 28) ^ ror64(a, 34) ^ ror64(a, 39);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void vigil_sha512_update(vigil_sha512_ctx *ctx, const uint8_t *data, size_t len) {
    size_t i, idx = (size_t)(ctx->count[0] & 127);
    ctx->count[0] += len;
    if (ctx->count[0] < len) ctx->count[1]++;
    for (i = 0; i < len; i++) {
        ctx->buffer[idx++] = data[i];
        if (idx == 128) { sha512_transform(ctx, ctx->buffer); idx = 0; }
    }
}

void vigil_sha512_final(vigil_sha512_ctx *ctx, uint8_t out[64]) {
    uint64_t bits_lo = ctx->count[0] * 8;
    uint64_t bits_hi = ctx->count[1] * 8 + (ctx->count[0] >> 61);
    size_t idx = (size_t)(ctx->count[0] & 127);
    ctx->buffer[idx++] = 0x80;
    if (idx > 112) {
        while (idx < 128) ctx->buffer[idx++] = 0;
        sha512_transform(ctx, ctx->buffer); idx = 0;
    }
    while (idx < 112) ctx->buffer[idx++] = 0;
    store_be64(ctx->buffer + 112, bits_hi);
    store_be64(ctx->buffer + 120, bits_lo);
    sha512_transform(ctx, ctx->buffer);
    for (int i = 0; i < 8; i++) store_be64(out + i * 8, ctx->state[i]);
}

void vigil_sha512(const uint8_t *data, size_t len, uint8_t out[64]) {
    vigil_sha512_ctx ctx;
    vigil_sha512_init(&ctx);
    vigil_sha512_update(&ctx, data, len);
    vigil_sha512_final(&ctx, out);
}

void vigil_sha384(const uint8_t *data, size_t len, uint8_t out[48]) {
    vigil_sha512_ctx ctx;
    ctx.state[0] = 0xcbbb9d5dc1059ed8ULL; ctx.state[1] = 0x629a292a367cd507ULL;
    ctx.state[2] = 0x9159015a3070dd17ULL; ctx.state[3] = 0x152fecd8f70e5939ULL;
    ctx.state[4] = 0x67332667ffc00b31ULL; ctx.state[5] = 0x8eb44a8768581511ULL;
    ctx.state[6] = 0xdb0c2e0d64f98fa7ULL; ctx.state[7] = 0x47b5481dbefa4fa4ULL;
    ctx.count[0] = ctx.count[1] = 0;
    vigil_sha512_update(&ctx, data, len);
    uint8_t full[64];
    vigil_sha512_final(&ctx, full);
    memcpy(out, full, 48);
}

/* ── HMAC-SHA256 ─────────────────────────────────────────────────── */

void vigil_hmac_sha256(const uint8_t *key, size_t key_len,
                      const uint8_t *data, size_t data_len,
                      uint8_t out[32]) {
    uint8_t k[64], o_pad[64], i_pad[64];
    vigil_sha256_ctx ctx;
    size_t i;

    memset(k, 0, 64);
    if (key_len > 64) {
        vigil_sha256(key, key_len, k);
    } else {
        memcpy(k, key, key_len);
    }

    for (i = 0; i < 64; i++) {
        o_pad[i] = k[i] ^ 0x5c;
        i_pad[i] = k[i] ^ 0x36;
    }

    vigil_sha256_init(&ctx);
    vigil_sha256_update(&ctx, i_pad, 64);
    vigil_sha256_update(&ctx, data, data_len);
    vigil_sha256_final(&ctx, out);

    vigil_sha256_init(&ctx);
    vigil_sha256_update(&ctx, o_pad, 64);
    vigil_sha256_update(&ctx, out, 32);
    vigil_sha256_final(&ctx, out);
}

/* ── PBKDF2-SHA256 ───────────────────────────────────────────────── */

void vigil_pbkdf2_sha256(const uint8_t *password, size_t pass_len,
                        const uint8_t *salt, size_t salt_len,
                        uint32_t iterations, uint8_t *out, size_t out_len) {
    uint8_t U[32], T[32], block_num[4];
    uint32_t block = 1;
    size_t offset = 0;

    while (offset < out_len) {
        store_be32(block_num, block);

        /* U_1 = PRF(Password, Salt || INT(i)) */
        vigil_sha256_ctx ctx;
        uint8_t k[64], i_pad[64];
        size_t i;

        memset(k, 0, 64);
        if (pass_len > 64) {
            vigil_sha256(password, pass_len, k);
        } else {
            memcpy(k, password, pass_len);
        }
        for (i = 0; i < 64; i++) i_pad[i] = k[i] ^ 0x36;

        vigil_sha256_init(&ctx);
        vigil_sha256_update(&ctx, i_pad, 64);
        vigil_sha256_update(&ctx, salt, salt_len);
        vigil_sha256_update(&ctx, block_num, 4);
        vigil_sha256_final(&ctx, U);

        uint8_t o_pad[64];
        for (i = 0; i < 64; i++) o_pad[i] = k[i] ^ 0x5c;
        vigil_sha256_init(&ctx);
        vigil_sha256_update(&ctx, o_pad, 64);
        vigil_sha256_update(&ctx, U, 32);
        vigil_sha256_final(&ctx, U);

        memcpy(T, U, 32);

        for (uint32_t j = 1; j < iterations; j++) {
            vigil_hmac_sha256(password, pass_len, U, 32, U);
            for (i = 0; i < 32; i++) T[i] ^= U[i];
        }

        size_t copy_len = out_len - offset;
        if (copy_len > 32) copy_len = 32;
        memcpy(out + offset, T, copy_len);
        offset += copy_len;
        block++;
    }
}

/* ── Secure Random ───────────────────────────────────────────────── */

void vigil_crypto_random_bytes(uint8_t *buf, size_t len) {
#ifdef _WIN32
    BCryptGenRandom(NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        size_t total = 0;
        while (total < len) {
            ssize_t n = read(fd, buf + total, len - total);
            if (n <= 0) break;
            total += (size_t)n;
        }
        close(fd);
    }
#endif
}

/* ── Constant-time Compare ───────────────────────────────────────── */

int vigil_crypto_constant_time_compare(const uint8_t *a, const uint8_t *b, size_t len) {
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

/* ── AES-256 Core ────────────────────────────────────────────────── */

static const uint8_t sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t rcon[11] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

static void aes256_key_expand(const uint8_t key[32], uint8_t rk[240]) {
    memcpy(rk, key, 32);
    uint8_t temp[4];
    int i = 8;
    while (i < 60) {
        memcpy(temp, rk + (i-1)*4, 4);
        if (i % 8 == 0) {
            uint8_t t = temp[0];
            temp[0] = sbox[temp[1]] ^ rcon[i/8];
            temp[1] = sbox[temp[2]];
            temp[2] = sbox[temp[3]];
            temp[3] = sbox[t];
        } else if (i % 8 == 4) {
            for (int j = 0; j < 4; j++) temp[j] = sbox[temp[j]];
        }
        for (int j = 0; j < 4; j++) rk[i*4+j] = rk[(i-8)*4+j] ^ temp[j];
        i++;
    }
}

static void aes_encrypt_block(const uint8_t rk[240], const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16];
    memcpy(s, in, 16);
    for (int i = 0; i < 16; i++) s[i] ^= rk[i];

    for (int round = 1; round < 14; round++) {
        uint8_t t[16];
        /* SubBytes + ShiftRows */
        t[0] = sbox[s[0]]; t[1] = sbox[s[5]]; t[2] = sbox[s[10]]; t[3] = sbox[s[15]];
        t[4] = sbox[s[4]]; t[5] = sbox[s[9]]; t[6] = sbox[s[14]]; t[7] = sbox[s[3]];
        t[8] = sbox[s[8]]; t[9] = sbox[s[13]]; t[10] = sbox[s[2]]; t[11] = sbox[s[7]];
        t[12] = sbox[s[12]]; t[13] = sbox[s[1]]; t[14] = sbox[s[6]]; t[15] = sbox[s[11]];
        /* MixColumns */
        for (int c = 0; c < 4; c++) {
            uint8_t *col = t + c*4;
            uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
            uint8_t x2_0 = (a0 << 1) ^ ((a0 >> 7) * 0x1b);
            uint8_t x2_1 = (a1 << 1) ^ ((a1 >> 7) * 0x1b);
            uint8_t x2_2 = (a2 << 1) ^ ((a2 >> 7) * 0x1b);
            uint8_t x2_3 = (a3 << 1) ^ ((a3 >> 7) * 0x1b);
            col[0] = x2_0 ^ x2_1 ^ a1 ^ a2 ^ a3;
            col[1] = a0 ^ x2_1 ^ x2_2 ^ a2 ^ a3;
            col[2] = a0 ^ a1 ^ x2_2 ^ x2_3 ^ a3;
            col[3] = x2_0 ^ a0 ^ a1 ^ a2 ^ x2_3;
        }
        /* AddRoundKey */
        for (int i = 0; i < 16; i++) s[i] = t[i] ^ rk[round*16+i];
    }
    /* Final round (no MixColumns) */
    out[0] = sbox[s[0]] ^ rk[224]; out[1] = sbox[s[5]] ^ rk[225];
    out[2] = sbox[s[10]] ^ rk[226]; out[3] = sbox[s[15]] ^ rk[227];
    out[4] = sbox[s[4]] ^ rk[228]; out[5] = sbox[s[9]] ^ rk[229];
    out[6] = sbox[s[14]] ^ rk[230]; out[7] = sbox[s[3]] ^ rk[231];
    out[8] = sbox[s[8]] ^ rk[232]; out[9] = sbox[s[13]] ^ rk[233];
    out[10] = sbox[s[2]] ^ rk[234]; out[11] = sbox[s[7]] ^ rk[235];
    out[12] = sbox[s[12]] ^ rk[236]; out[13] = sbox[s[1]] ^ rk[237];
    out[14] = sbox[s[6]] ^ rk[238]; out[15] = sbox[s[11]] ^ rk[239];
}

/* ── GCM Mode ────────────────────────────────────────────────────── */

static void gcm_mult(uint8_t *x, const uint8_t *h) {
    uint8_t z[16] = {0};
    for (int i = 0; i < 128; i++) {
        if ((h[i/8] >> (7 - i%8)) & 1) {
            for (int j = 0; j < 16; j++) z[j] ^= x[j];
        }
        int lsb = x[15] & 1;
        for (int j = 15; j > 0; j--) x[j] = (x[j] >> 1) | (x[j-1] << 7);
        x[0] >>= 1;
        if (lsb) x[0] ^= 0xe1;
    }
    memcpy(x, z, 16);
}

static void gcm_ghash(const uint8_t *h, const uint8_t *data, size_t len, uint8_t *out) {
    memset(out, 0, 16);
    size_t blocks = len / 16;
    for (size_t i = 0; i < blocks; i++) {
        for (int j = 0; j < 16; j++) out[j] ^= data[i*16+j];
        gcm_mult(out, h);
    }
    if (len % 16) {
        for (size_t j = 0; j < len % 16; j++) out[j] ^= data[blocks*16+j];
        gcm_mult(out, h);
    }
}

int vigil_aes256_gcm_encrypt(const uint8_t key[32], const uint8_t *nonce, size_t nonce_len,
                            const uint8_t *plaintext, size_t pt_len,
                            const uint8_t *aad, size_t aad_len,
                            uint8_t *ciphertext, uint8_t tag[16]) {
    uint8_t rk[240], h[16] = {0}, j0[16], counter[16], enc_ctr[16];

    aes256_key_expand(key, rk);
    aes_encrypt_block(rk, h, h);

    /* Compute J0 */
    if (nonce_len == 12) {
        memcpy(j0, nonce, 12);
        j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
    } else {
        uint8_t len_block[16] = {0};
        store_be64(len_block + 8, nonce_len * 8);
        gcm_ghash(h, nonce, nonce_len, j0);
        for (int i = 0; i < 16; i++) j0[i] ^= len_block[i];
        gcm_mult(j0, h);
    }

    /* Encrypt plaintext with counter mode */
    memcpy(counter, j0, 16);
    for (size_t i = 0; i < pt_len; i += 16) {
        /* Increment counter */
        for (int j = 15; j >= 12; j--) if (++counter[j]) break;
        aes_encrypt_block(rk, counter, enc_ctr);
        size_t block_len = (pt_len - i < 16) ? pt_len - i : 16;
        for (size_t j = 0; j < block_len; j++) ciphertext[i+j] = plaintext[i+j] ^ enc_ctr[j];
    }

    /* Compute tag */
    uint8_t ghash_in[16], s[16];
    size_t aad_padded = ((aad_len + 15) / 16) * 16;
    size_t ct_padded = ((pt_len + 15) / 16) * 16;

    memset(s, 0, 16);
    /* GHASH AAD */
    for (size_t i = 0; i < aad_len; i += 16) {
        memset(ghash_in, 0, 16);
        size_t block_len = (aad_len - i < 16) ? aad_len - i : 16;
        memcpy(ghash_in, aad + i, block_len);
        for (int j = 0; j < 16; j++) s[j] ^= ghash_in[j];
        gcm_mult(s, h);
    }
    /* GHASH ciphertext */
    for (size_t i = 0; i < pt_len; i += 16) {
        memset(ghash_in, 0, 16);
        size_t block_len = (pt_len - i < 16) ? pt_len - i : 16;
        memcpy(ghash_in, ciphertext + i, block_len);
        for (int j = 0; j < 16; j++) s[j] ^= ghash_in[j];
        gcm_mult(s, h);
    }
    /* GHASH lengths */
    uint8_t len_block[16];
    store_be64(len_block, aad_len * 8);
    store_be64(len_block + 8, pt_len * 8);
    for (int j = 0; j < 16; j++) s[j] ^= len_block[j];
    gcm_mult(s, h);

    /* Encrypt S with J0 to get tag */
    aes_encrypt_block(rk, j0, enc_ctr);
    for (int i = 0; i < 16; i++) tag[i] = s[i] ^ enc_ctr[i];

    (void)aad_padded; (void)ct_padded;
    return 0;
}

int vigil_aes256_gcm_decrypt(const uint8_t key[32], const uint8_t *nonce, size_t nonce_len,
                            const uint8_t *ciphertext, size_t ct_len,
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t tag[16], uint8_t *plaintext) {
    uint8_t computed_tag[16];
    uint8_t rk[240], h[16] = {0}, j0[16], counter[16], enc_ctr[16];

    aes256_key_expand(key, rk);
    aes_encrypt_block(rk, h, h);

    /* Compute J0 */
    if (nonce_len == 12) {
        memcpy(j0, nonce, 12);
        j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
    } else {
        uint8_t len_block[16] = {0};
        store_be64(len_block + 8, nonce_len * 8);
        gcm_ghash(h, nonce, nonce_len, j0);
        for (int i = 0; i < 16; i++) j0[i] ^= len_block[i];
        gcm_mult(j0, h);
    }

    /* Compute expected tag first */
    uint8_t ghash_in[16], s[16];
    memset(s, 0, 16);
    for (size_t i = 0; i < aad_len; i += 16) {
        memset(ghash_in, 0, 16);
        size_t block_len = (aad_len - i < 16) ? aad_len - i : 16;
        memcpy(ghash_in, aad + i, block_len);
        for (int j = 0; j < 16; j++) s[j] ^= ghash_in[j];
        gcm_mult(s, h);
    }
    for (size_t i = 0; i < ct_len; i += 16) {
        memset(ghash_in, 0, 16);
        size_t block_len = (ct_len - i < 16) ? ct_len - i : 16;
        memcpy(ghash_in, ciphertext + i, block_len);
        for (int j = 0; j < 16; j++) s[j] ^= ghash_in[j];
        gcm_mult(s, h);
    }
    uint8_t len_block[16];
    store_be64(len_block, aad_len * 8);
    store_be64(len_block + 8, ct_len * 8);
    for (int j = 0; j < 16; j++) s[j] ^= len_block[j];
    gcm_mult(s, h);
    aes_encrypt_block(rk, j0, enc_ctr);
    for (int i = 0; i < 16; i++) computed_tag[i] = s[i] ^ enc_ctr[i];

    /* Verify tag */
    if (!vigil_crypto_constant_time_compare(tag, computed_tag, 16)) {
        return -1; /* Authentication failed */
    }

    /* Decrypt */
    memcpy(counter, j0, 16);
    for (size_t i = 0; i < ct_len; i += 16) {
        for (int j = 15; j >= 12; j--) if (++counter[j]) break;
        aes_encrypt_block(rk, counter, enc_ctr);
        size_t block_len = (ct_len - i < 16) ? ct_len - i : 16;
        for (size_t j = 0; j < block_len; j++) plaintext[i+j] = ciphertext[i+j] ^ enc_ctr[j];
    }

    return 0;
}

/* ── Password-based encryption ───────────────────────────────────── */

#define PBKDF2_ITERATIONS 100000

size_t vigil_password_encrypt(const uint8_t *password, size_t pass_len,
                             const uint8_t *plaintext, size_t pt_len,
                             uint8_t *out) {
    uint8_t salt[16], nonce[12], key[32], tag[16];

    vigil_crypto_random_bytes(salt, 16);
    vigil_crypto_random_bytes(nonce, 12);
    vigil_pbkdf2_sha256(password, pass_len, salt, 16, PBKDF2_ITERATIONS, key, 32);

    memcpy(out, salt, 16);
    memcpy(out + 16, nonce, 12);

    if (vigil_aes256_gcm_encrypt(key, nonce, 12, plaintext, pt_len, NULL, 0,
                                 out + 28, tag) != 0) {
        return 0;
    }
    memcpy(out + 28 + pt_len, tag, 16);
    return 28 + pt_len + 16;
}

size_t vigil_password_decrypt(const uint8_t *password, size_t pass_len,
                             const uint8_t *ciphertext, size_t ct_len,
                             uint8_t *out) {
    if (ct_len < 44) return 0; /* salt(16) + nonce(12) + tag(16) minimum */

    const uint8_t *salt = ciphertext;
    const uint8_t *nonce = ciphertext + 16;
    const uint8_t *enc_data = ciphertext + 28;
    size_t enc_len = ct_len - 44;
    const uint8_t *tag = ciphertext + ct_len - 16;

    uint8_t key[32];
    vigil_pbkdf2_sha256(password, pass_len, salt, 16, PBKDF2_ITERATIONS, key, 32);

    if (vigil_aes256_gcm_decrypt(key, nonce, 12, enc_data, enc_len, NULL, 0, tag, out) != 0) {
        return 0;
    }
    return enc_len;
}
