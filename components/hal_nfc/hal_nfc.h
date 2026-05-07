#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// PN532 NFC HAL — I2C, GP5=SDA GP6=SCL

#define NFC_TX_BUFFER_LEN 220   // max raw tx bytes held in RAM

typedef struct {
    uint8_t uid[7];
    uint8_t uid_len;
} nfc_tag_t;

typedef struct {
    char from[45];
    char to[45];
    uint64_t lamports;
    uint8_t tx_bytes[220];
    uint16_t tx_len;
    char nonce[37];
    bool valid;
} nfc_tx_payload_t;

typedef struct {
    uint8_t seed[32];
    bool valid;
} nfc_key_import_t;

void hal_nfc_init(void);
bool hal_nfc_ensure_init(void);
void hal_nfc_shutdown(void);
bool hal_nfc_is_ready(void);

// Wait for NFC tag (non-blocking, returns false if no tag)
bool hal_nfc_wait_tag(uint16_t timeout_ms, nfc_tag_t *tag);

// Process wallet NDEF records — populates g_nfc_tx or g_nfc_key_import
bool hal_nfc_process_ndef(void);

// Write sign response back to phone (JSON base64 — matches Android NdefProtocol.kt)
bool hal_nfc_write_sign_response(const uint8_t sig[64], const char *nonce);

// Write solvare:wallet NDEF so Android app can read device pubkey
bool hal_nfc_write_wallet_ndef(const uint8_t pubkey[32]);

// Global payloads
extern nfc_tx_payload_t g_nfc_tx;
extern nfc_key_import_t g_nfc_key_import;
