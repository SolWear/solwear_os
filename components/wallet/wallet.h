#pragma once
#include <stdint.h>
#include <stdbool.h>

// ============================================================
// Wallet — encrypted Ed25519 key in NVS
// NVS namespace "wallet", keys: salt, iv, seed_enc, pubkey, name
// ============================================================

bool wallet_is_onboarded(void);

// Generate new keypair, encrypt + store; returns false on NVS error
bool wallet_generate(const char *password, const char *name);

// Import existing 32-byte seed, encrypt + store
bool wallet_import_seed(const uint8_t seed[32], const char *password, const char *name);

// Decrypt private key into RAM; false = wrong password
bool wallet_unlock(const char *password);
void wallet_lock(void);
bool wallet_is_unlocked(void);

// Pubkey always available (stored plaintext in NVS)
const uint8_t *wallet_pubkey(void);
const char    *wallet_name(void);

// Sign tx bytes with unlocked key; false if locked
bool wallet_sign(const uint8_t *tx, size_t len, uint8_t sig[64]);

// Change password (re-encrypt seed)
bool wallet_change_password(const char *old_pw, const char *new_pw);

// Load public info (pubkey + name) — call at boot, no password needed
bool wallet_load_public(void);
