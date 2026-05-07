#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// PBKDF2-HMAC-SHA256, 32-byte output key
void crypto_derive_key(const char *password, const uint8_t salt[16], uint8_t key[32]);

// AES-256-CBC encrypt/decrypt (len must be multiple of 16)
bool crypto_aes_encrypt(const uint8_t key[32], const uint8_t iv[16],
                        const uint8_t *in, uint8_t *out, size_t len);
bool crypto_aes_decrypt(const uint8_t key[32], const uint8_t iv[16],
                        const uint8_t *in, uint8_t *out, size_t len);

// Ed25519 via mbedTLS (RFC 8032)
// seed = 32 bytes private seed; pubkey = 32 bytes
void crypto_ed25519_pubkey(const uint8_t seed[32], uint8_t pubkey[32]);
bool crypto_ed25519_sign(const uint8_t seed[32], const uint8_t pubkey[32],
                         const uint8_t *msg, size_t msg_len, uint8_t sig[64]);

// Hardware RNG
void crypto_random(uint8_t *buf, size_t len);

// Secure wipe
void crypto_wipe(void *buf, size_t len);
