#include "crypto.h"
#include "aes/esp_aes.h"
#include "sha/sha_parallel_engine.h"
#include "esp_random.h"
#include <string.h>
#include <stdlib.h>

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

void crypto_ed25519_pubkey(const uint8_t seed[32], uint8_t pubkey[32])
{
    // Stub — Ed25519 not yet implemented (tweetnacl pending)
    (void)seed;
    memset(pubkey, 0, 32);
}

bool crypto_ed25519_sign(const uint8_t seed[32], const uint8_t pubkey[32],
                         const uint8_t *msg, size_t msg_len, uint8_t sig[64])
{
    (void)seed; (void)pubkey; (void)msg; (void)msg_len;
    memset(sig, 0, 64);
    return false;
}
