#include "crypto.h"
#include "aes/esp_aes.h"
#include "sha/sha_parallel_engine.h"
#include "esp_random.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

void crypto_random(uint8_t *buf, size_t len)
{
    esp_fill_random(buf, len);
}

void crypto_wipe(void *buf, size_t len)
{
    volatile uint8_t *p = (volatile uint8_t *)buf;
    while (len--) *p++ = 0;
}

// HMAC-SHA256 using ESP hardware SHA.
// dlen must be <= 96 (our PBKDF2 usage: 20 bytes or 32 bytes).
static void hmac_sha256(const uint8_t *key, size_t klen,
                        const uint8_t *data, size_t dlen,
                        uint8_t out[32])
{
    uint8_t k_ipad[64] = {0};
    uint8_t k_opad[64] = {0};

    if (klen > 64) {
        uint8_t k_hashed[32];
        esp_sha(SHA2_256, key, klen, k_hashed);
        memcpy(k_ipad, k_hashed, 32);
    } else {
        memcpy(k_ipad, key, klen);
    }
    memcpy(k_opad, k_ipad, 64);
    for (int i = 0; i < 64; i++) { k_ipad[i] ^= 0x36; k_opad[i] ^= 0x5C; }

    // Inner: SHA256(ipad || data)
    uint8_t inner_buf[64 + 96];
    if (dlen > 96) dlen = 96;
    memcpy(inner_buf, k_ipad, 64);
    memcpy(inner_buf + 64, data, dlen);
    uint8_t inner_hash[32];
    esp_sha(SHA2_256, inner_buf, 64 + dlen, inner_hash);

    // Outer: SHA256(opad || inner_hash)
    uint8_t outer_buf[96];
    memcpy(outer_buf, k_opad, 64);
    memcpy(outer_buf + 64, inner_hash, 32);
    esp_sha(SHA2_256, outer_buf, 96, out);
}

void crypto_derive_key(const char *password, const uint8_t salt[16], uint8_t key[32])
{
    size_t plen = strlen(password);

    // PBKDF2-HMAC-SHA256, single 32-byte block, 2000 iterations
    uint8_t salt_block[20];
    memcpy(salt_block, salt, 16);
    salt_block[16] = 0; salt_block[17] = 0;
    salt_block[18] = 0; salt_block[19] = 1;  // block index = 1

    uint8_t u[32];
    hmac_sha256((const uint8_t *)password, plen, salt_block, 20, u);
    memcpy(key, u, 32);

    for (uint32_t i = 1; i < 2000; i++) {
        uint8_t u_next[32];
        hmac_sha256((const uint8_t *)password, plen, u, 32, u_next);
        memcpy(u, u_next, 32);
        for (int j = 0; j < 32; j++) key[j] ^= u[j];
    }

    crypto_wipe(u, 32);
    crypto_wipe(salt_block, 20);
}

bool crypto_aes_encrypt(const uint8_t key[32], const uint8_t iv[16],
                        const uint8_t *in, uint8_t *out, size_t len)
{
    uint8_t iv_buf[16];
    memcpy(iv_buf, iv, 16);
    esp_aes_context ctx;
    esp_aes_init(&ctx);
    int r = esp_aes_setkey(&ctx, key, 256);
    if (r != 0) { esp_aes_free(&ctx); return false; }
    r = esp_aes_crypt_cbc(&ctx, ESP_AES_ENCRYPT, len, iv_buf, in, out);
    esp_aes_free(&ctx);
    return r == 0;
}

bool crypto_aes_decrypt(const uint8_t key[32], const uint8_t iv[16],
                        const uint8_t *in, uint8_t *out, size_t len)
{
    uint8_t iv_buf[16];
    memcpy(iv_buf, iv, 16);
    esp_aes_context ctx;
    esp_aes_init(&ctx);
    int r = esp_aes_setkey(&ctx, key, 256);
    if (r != 0) { esp_aes_free(&ctx); return false; }
    r = esp_aes_crypt_cbc(&ctx, ESP_AES_DECRYPT, len, iv_buf, in, out);
    esp_aes_free(&ctx);
    return r == 0;
}

typedef int64_t fe25519[16];

static const fe25519 fe0 = {0};
static const fe25519 fe1 = {1};
static const fe25519 ed_d = {0x78a3,0x1359,0x4dca,0x75eb,0xd8ab,0x4141,0x0a4d,0x0070,0xe898,0x7779,0x4079,0x8cc7,0xfe73,0x2b6f,0x6cee,0x5203};
static const fe25519 ed_i = {0xa0b0,0x4a0e,0x1b27,0xc4ee,0xe478,0xad2f,0x1806,0x2f43,0xd7a7,0x3dfb,0x0099,0x2b4d,0xdf0b,0x4fc1,0x2480,0x2b83};
static const uint8_t ed_base_y[32] = {0x58,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66};
static const uint8_t ed_l[32] = {0xed,0xd3,0xf5,0x5c,0x1a,0x63,0x12,0x58,0xd6,0x9c,0xf7,0xa2,0xde,0xf9,0xde,0x14,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x10};

static void fe_copy(fe25519 o, const fe25519 a) { memcpy(o, a, sizeof(fe25519)); }
static void fe_add(fe25519 o, const fe25519 a, const fe25519 b) { for (int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
static void fe_sub(fe25519 o, const fe25519 a, const fe25519 b) { for (int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }

static void car25519(fe25519 o)
{
    for (int i = 0; i < 16; i++) {
        o[i] += 1LL << 16;
        int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

static void sel25519(fe25519 p, fe25519 q, int b)
{
    int64_t c = ~(int64_t)(b - 1);
    for (int i = 0; i < 16; i++) {
        int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void pack25519(uint8_t *o, const fe25519 n)
{
    fe25519 m, t;
    fe_copy(t, n);
    car25519(t); car25519(t); car25519(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i] = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

static void unpack25519(fe25519 o, const uint8_t *n)
{
    for (int i = 0; i < 16; i++) o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}

static void fe_mul(fe25519 o, const fe25519 a, const fe25519 b)
{
    int64_t t[31] = {0};
    for (int i = 0; i < 16; i++) for (int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; i++) o[i] = t[i];
    car25519(o); car25519(o);
}

static void fe_square(fe25519 o, const fe25519 a) { fe_mul(o, a, a); }

static void inv25519(fe25519 o, const fe25519 i)
{
    fe25519 c;
    fe_copy(c, i);
    for (int a = 253; a >= 0; a--) {
        fe_square(c, c);
        if (a != 2 && a != 4) fe_mul(c, c, i);
    }
    fe_copy(o, c);
}

static int neq25519(const fe25519 a, const fe25519 b)
{
    uint8_t c[32], d[32];
    pack25519(c, a);
    pack25519(d, b);
    return memcmp(c, d, 32);
}

static int par25519(const fe25519 a)
{
    uint8_t d[32];
    pack25519(d, a);
    return d[0] & 1;
}

static void pow2523(fe25519 o, const fe25519 i)
{
    fe25519 c;
    fe_copy(c, i);
    for (int a = 250; a >= 0; a--) {
        fe_square(c, c);
        if (a != 1) fe_mul(c, c, i);
    }
    fe_copy(o, c);
}

static int unpackneg(fe25519 r[4], const uint8_t p[32])
{
    fe25519 t, chk, num, den, den2, den4, den6;
    fe_copy(r[2], fe1);
    unpack25519(r[1], p);
    fe_square(num, r[1]);
    fe_mul(den, num, ed_d);
    fe_sub(num, num, r[2]);
    fe_add(den, r[2], den);
    fe_square(den2, den);
    fe_square(den4, den2);
    fe_mul(den6, den4, den2);
    fe_mul(t, den6, num);
    fe_mul(t, t, den);
    pow2523(t, t);
    fe_mul(t, t, num);
    fe_mul(t, t, den);
    fe_mul(t, t, den);
    fe_mul(r[0], t, den);
    fe_square(chk, r[0]);
    fe_mul(chk, chk, den);
    if (neq25519(chk, num)) fe_mul(r[0], r[0], ed_i);
    fe_square(chk, r[0]);
    fe_mul(chk, chk, den);
    if (neq25519(chk, num)) return -1;
    if (par25519(r[0]) == (p[31] >> 7)) fe_sub(r[0], fe0, r[0]);
    fe_mul(r[3], r[0], r[1]);
    return 0;
}

static void ed_add(fe25519 p[4], fe25519 q[4])
{
    fe25519 a, b, c, e, f, g, h, t;
    fe_sub(a, p[1], p[0]); fe_sub(t, q[1], q[0]); fe_mul(a, a, t);
    fe_add(b, p[0], p[1]); fe_add(t, q[0], q[1]); fe_mul(b, b, t);
    fe_mul(c, p[3], q[3]); fe_mul(c, c, ed_d); fe_add(c, c, c);
    fe_mul(e, p[2], q[2]); fe_add(e, e, e);
    fe_sub(f, b, a); fe_sub(g, e, c); fe_add(h, e, c); fe_add(p[0], b, a);
    fe_mul(p[0], p[0], g); fe_mul(p[1], f, h); fe_mul(p[2], g, h); fe_mul(p[3], f, p[0]);
}

static void ed_cswap(fe25519 p[4], fe25519 q[4], int b)
{
    for (int i = 0; i < 4; i++) sel25519(p[i], q[i], b);
}

static void ed_scalarmult(fe25519 p[4], fe25519 q[4], const uint8_t *s)
{
    fe_copy(p[0], fe0); fe_copy(p[1], fe1); fe_copy(p[2], fe1); fe_copy(p[3], fe0);
    for (int i = 255; i >= 0; i--) {
        int b = (s[i >> 3] >> (i & 7)) & 1;
        ed_cswap(p, q, b);
        ed_add(q, p);
        ed_add(p, p);
        ed_cswap(p, q, b);
    }
}

static void ed_scalarbase(fe25519 p[4], const uint8_t *s)
{
    fe25519 q[4];
    unpackneg(q, ed_base_y);
    ed_scalarmult(p, q, s);
}

static void ed_pack(uint8_t *r, fe25519 p[4])
{
    fe25519 tx, ty, zi;
    inv25519(zi, p[2]);
    fe_mul(tx, p[0], zi);
    fe_mul(ty, p[1], zi);
    pack25519(r, ty);
    r[31] ^= (uint8_t)(par25519(tx) << 7);
}

static void mod_l(uint8_t *r, int64_t x[64])
{
    int64_t carry;
    for (int i = 63; i >= 32; i--) {
        carry = 0;
        for (int j = i - 32; j < i - 12; j++) {
            x[j] += carry - 16 * x[i] * ed_l[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry << 8;
        }
        x[i - 12] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (int j = 0; j < 32; j++) {
        x[j] += carry;
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (int j = 0; j < 32; j++) x[j] -= carry * ed_l[j];
    for (int i = 0; i < 32; i++) {
        x[i + 1] += x[i] >> 8;
        r[i] = (uint8_t)(x[i] & 255);
    }
}

static void reduce_l(uint8_t *r)
{
    int64_t x[64];
    for (int i = 0; i < 64; i++) x[i] = r[i];
    memset(r, 0, 64);
    mod_l(r, x);
}

void crypto_ed25519_pubkey(const uint8_t seed[32], uint8_t pubkey[32])
{
    uint8_t h[64];
    fe25519 p[4];
    esp_sha(SHA2_512, seed, 32, h);
    h[0] &= 248;
    h[31] &= 127;
    h[31] |= 64;
    ed_scalarbase(p, h);
    ed_pack(pubkey, p);
    crypto_wipe(h, sizeof(h));
}

bool crypto_ed25519_sign(const uint8_t seed[32], const uint8_t pubkey[32],
                         const uint8_t *msg, size_t msg_len, uint8_t sig[64])
{
    if (!seed || !pubkey || (!msg && msg_len > 0) || !sig) return false;

    uint8_t d_hash[64], r_hash[64], h_hash[64];
    esp_sha(SHA2_512, seed, 32, d_hash);
    d_hash[0] &= 248;
    d_hash[31] &= 127;
    d_hash[31] |= 64;

    uint8_t *tmp = (uint8_t *)malloc(32 + msg_len);
    if (!tmp) return false;
    memcpy(tmp, d_hash + 32, 32);
    if (msg_len > 0) memcpy(tmp + 32, msg, msg_len);
    esp_sha(SHA2_512, tmp, 32 + msg_len, r_hash);
    crypto_wipe(tmp, 32 + msg_len);
    free(tmp);
    reduce_l(r_hash);

    fe25519 p[4];
    ed_scalarbase(p, r_hash);
    ed_pack(sig, p);

    tmp = (uint8_t *)malloc(64 + msg_len);
    if (!tmp) {
        crypto_wipe(d_hash, sizeof(d_hash));
        crypto_wipe(r_hash, sizeof(r_hash));
        return false;
    }
    memcpy(tmp, sig, 32);
    memcpy(tmp + 32, pubkey, 32);
    if (msg_len > 0) memcpy(tmp + 64, msg, msg_len);
    esp_sha(SHA2_512, tmp, 64 + msg_len, h_hash);
    crypto_wipe(tmp, 64 + msg_len);
    free(tmp);
    reduce_l(h_hash);

    int64_t x[64] = {0};
    for (int i = 0; i < 32; i++) x[i] = r_hash[i];
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            x[i + j] += (int64_t)h_hash[i] * d_hash[j];
        }
    }
    mod_l(sig + 32, x);

    crypto_wipe(d_hash, sizeof(d_hash));
    crypto_wipe(r_hash, sizeof(r_hash));
    crypto_wipe(h_hash, sizeof(h_hash));
    crypto_wipe(x, sizeof(x));
    return true;
}
