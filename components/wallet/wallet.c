#include "wallet.h"
#include "crypto.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "sha/sha_parallel_engine.h"
#include <string.h>

static const char *TAG = "wallet";
#define NVS_NS "wallet"

static uint8_t s_pubkey[32]  = {};
static uint8_t s_seed[32]    = {};
static char    s_name[17]    = {};
static bool    s_unlocked    = false;

bool wallet_is_onboarded(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = 32;
    bool found = (nvs_get_blob(h, "pubkey", s_pubkey, &len) == ESP_OK);
    nvs_close(h);
    return found;
}

static bool save_to_nvs(const uint8_t seed[32], const char *password, const char *name)
{
    uint8_t salt[16], iv[16], key[32], enc_seed[32];
    crypto_random(salt, 16);
    crypto_random(iv,   16);
    crypto_derive_key(password, salt, key);

    // SHA-256(seed) used as verification token until real Ed25519 is wired in.
    // Replace with crypto_ed25519_pubkey() once tweetnacl is added.
    uint8_t pubkey[32];
    esp_sha(SHA2_256, seed, 32, pubkey);

    if (!crypto_aes_encrypt(key, iv, seed, enc_seed, 32)) {
        crypto_wipe(key, 32);
        return false;
    }
    crypto_wipe(key, 32);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;

    nvs_set_blob(h, "salt",     salt,     16);
    nvs_set_blob(h, "iv",       iv,       16);
    nvs_set_blob(h, "seed_enc", enc_seed, 32);
    nvs_set_blob(h, "pubkey",   pubkey,   32);
    nvs_set_str (h, "name",     name);
    nvs_commit(h);
    nvs_close(h);

    memcpy(s_pubkey, pubkey, 32);
    size_t nlen = strlen(name);
    if (nlen > 16) nlen = 16;
    memcpy(s_name, name, nlen);
    s_name[nlen] = '\0';

    crypto_wipe(enc_seed, 32);
    return true;
}

bool wallet_generate(const char *password, const char *name)
{
    uint8_t seed[32];
    crypto_random(seed, 32);
    bool ok = save_to_nvs(seed, password, name);
    crypto_wipe(seed, 32);
    return ok;
}

bool wallet_import_seed(const uint8_t seed[32], const char *password, const char *name)
{
    return save_to_nvs(seed, password, name);
}

bool wallet_load_public(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = 32;
    bool ok = (nvs_get_blob(h, "pubkey", s_pubkey, &len) == ESP_OK);
    if (ok) {
        size_t nlen = sizeof(s_name) - 1;
        nvs_get_str(h, "name", s_name, &nlen);
    }
    nvs_close(h);
    return ok;
}

bool wallet_unlock(const char *password)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;

    uint8_t salt[16], iv[16], enc_seed[32], pubkey_ref[32];
    size_t len = 16;
    if (nvs_get_blob(h, "salt",     salt,      &len) != ESP_OK) { nvs_close(h); return false; }
    if (nvs_get_blob(h, "iv",       iv,        &len) != ESP_OK) { nvs_close(h); return false; }
    len = 32;
    if (nvs_get_blob(h, "seed_enc", enc_seed,  &len) != ESP_OK) { nvs_close(h); return false; }
    if (nvs_get_blob(h, "pubkey",   pubkey_ref, &len) != ESP_OK) { nvs_close(h); return false; }
    nvs_close(h);

    uint8_t key[32], dec_seed[32];
    crypto_derive_key(password, salt, key);
    bool ok = crypto_aes_decrypt(key, iv, enc_seed, dec_seed, 32);
    crypto_wipe(key, 32);
    if (!ok) return false;

    // Verify password by checking SHA-256(decrypted_seed) == stored pubkey_ref
    // (We store SHA-256(seed) as "pubkey" until real Ed25519 is implemented)
    // This ensures wrong password → wrong AES decrypt → wrong seed → wrong hash → rejected
    uint8_t check_hash[32];
    esp_sha(SHA2_256, dec_seed, 32, check_hash);
    if (memcmp(check_hash, pubkey_ref, 32) != 0) {
        ESP_LOGW(TAG, "Wrong password");
        crypto_wipe(dec_seed, 32);
        return false;
    }

    memcpy(s_seed, dec_seed, 32);
    crypto_wipe(dec_seed, 32);
    s_unlocked = true;
    ESP_LOGI(TAG, "Wallet unlocked");
    return true;
}

void wallet_lock(void)
{
    crypto_wipe(s_seed, 32);
    s_unlocked = false;
    ESP_LOGI(TAG, "Wallet locked");
}

bool wallet_is_unlocked(void) { return s_unlocked; }

const uint8_t *wallet_pubkey(void) { return s_pubkey; }
const char    *wallet_name(void)   { return s_name; }

bool wallet_sign(const uint8_t *tx, size_t len, uint8_t sig[64])
{
    if (!s_unlocked) return false;
    return crypto_ed25519_sign(s_seed, s_pubkey, tx, len, sig);
}

bool wallet_change_password(const char *old_pw, const char *new_pw)
{
    if (!wallet_unlock(old_pw)) return false;
    uint8_t seed_copy[32];
    memcpy(seed_copy, s_seed, 32);
    wallet_lock();
    bool ok = save_to_nvs(seed_copy, new_pw, s_name);
    crypto_wipe(seed_copy, 32);
    return ok;
}
