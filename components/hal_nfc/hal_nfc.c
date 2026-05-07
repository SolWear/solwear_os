#include "hal_nfc.h"
#include "driver/i2c.h"  // legacy I2C driver, still valid in IDF 6.x via esp_driver_i2c
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "hal_nfc";
static bool s_ready = false;

nfc_tx_payload_t  g_nfc_tx         = {};
nfc_key_import_t  g_nfc_key_import = {};

#define I2C_PORT   I2C_NUM_0
#define PIN_SDA    5
#define PIN_SCL    6
#define PN532_ADDR 0x24

// ── Base64 ───────────────────────────────────────────────────────────────
static const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static size_t b64_decode(const char *in, size_t in_len,
                          uint8_t *out, size_t out_max)
{
    size_t out_len = 0;
    for (size_t i = 0; i + 3 < in_len && out_len < out_max; i += 4) {
        int v0 = b64_val(in[i]),   v1 = b64_val(in[i+1]);
        int v2 = (in[i+2]!='=') ? b64_val(in[i+2]) : 0;
        int v3 = (in[i+3]!='=') ? b64_val(in[i+3]) : 0;
        if (v0 < 0 || v1 < 0) break;
        if (out_len < out_max) out[out_len++] = (uint8_t)((v0<<2)|(v1>>4));
        if (out_len < out_max && in[i+2]!='=') out[out_len++] = (uint8_t)((v1<<4)|(v2>>2));
        if (out_len < out_max && in[i+3]!='=') out[out_len++] = (uint8_t)((v2<<6)|v3);
    }
    return out_len;
}

static size_t b64_encode(const uint8_t *in, size_t in_len,
                          char *out, size_t out_max)
{
    size_t out_len = 0;
    for (size_t i = 0; i < in_len; i += 3) {
        uint8_t b0 = in[i];
        uint8_t b1 = (i+1 < in_len) ? in[i+1] : 0;
        uint8_t b2 = (i+2 < in_len) ? in[i+2] : 0;
        if (out_len + 4 >= out_max) break;
        out[out_len++] = kB64[b0 >> 2];
        out[out_len++] = kB64[((b0&3)<<4)|(b1>>4)];
        out[out_len++] = (i+1<in_len) ? kB64[((b1&0xF)<<2)|(b2>>6)] : '=';
        out[out_len++] = (i+2<in_len) ? kB64[b2&0x3F]                : '=';
    }
    out[out_len] = '\0';
    return out_len;
}

// ── Init ─────────────────────────────────────────────────────────────────

void hal_nfc_init(void)
{
    ESP_LOGI(TAG, "NFC lazy — powers up on demand");
}

bool hal_nfc_ensure_init(void)
{
    if (s_ready) return true;

    i2c_config_t cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = PIN_SDA,
        .scl_io_num       = PIN_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    if (i2c_param_config(I2C_PORT, &cfg) != ESP_OK) return false;
    if (i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) return false;

    uint8_t cmd[] = {0x00,0x00,0xFF,0x02,0xFE,0xD4,0x02,0x2A,0x00};
    uint8_t resp[12] = {};
    i2c_master_write_to_device(I2C_PORT, PN532_ADDR, cmd, sizeof(cmd), pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(30));
    i2c_master_read_from_device(I2C_PORT, PN532_ADDR, resp, sizeof(resp), pdMS_TO_TICKS(100));

    if (resp[1] == 0x00 && resp[2] == 0xFF) {
        s_ready = true;
        ESP_LOGI(TAG, "PN532 v%d.%d", resp[7], resp[8]);
        return true;
    }
    ESP_LOGW(TAG, "PN532 not found");
    return false;
}

void hal_nfc_shutdown(void)
{
    if (!s_ready) return;
    i2c_driver_delete(I2C_PORT);
    s_ready = false;
}

bool hal_nfc_is_ready(void) { return s_ready; }

bool hal_nfc_wait_tag(uint16_t timeout_ms, nfc_tag_t *tag)
{
    if (!s_ready || !tag) return false;
    uint8_t cmd[] = {0x00,0x00,0xFF,0x04,0xFC,0xD4,0x4A,0x01,0x00,0xE1,0x00};
    i2c_master_write_to_device(I2C_PORT, PN532_ADDR, cmd, sizeof(cmd), pdMS_TO_TICKS(timeout_ms));
    uint32_t w = timeout_ms < 60 ? timeout_ms : 60;
    vTaskDelay(pdMS_TO_TICKS(w));
    uint8_t resp[20] = {};
    if (i2c_master_read_from_device(I2C_PORT, PN532_ADDR, resp, sizeof(resp), pdMS_TO_TICKS(50)) != ESP_OK)
        return false;
    if (resp[6]!=0xD5 || resp[7]!=0x4B || resp[8]==0) return false;
    tag->uid_len = resp[12]; if (tag->uid_len > 7) tag->uid_len = 7;
    memcpy(tag->uid, &resp[13], tag->uid_len);
    return true;
}

// ── NDEF parser — aligned with Android NdefProtocol.kt ───────────────────
// Android External Type records: type = "solvare:sign_request"
// Payload = raw JSON bytes: {"version":1,"tx_bytes":"<base64>"}

static bool parse_ndef(const uint8_t *data, size_t len)
{
    bool found = false;
    size_t i = 0;

    while (i < len) {
        if (data[i] == 0xFE || data[i] == 0x00) { if (data[i]==0xFE) break; i++; continue; }
        uint8_t flags    = data[i++]; if (i >= len) break;
        uint8_t type_len = data[i++]; if (i >= len) break;

        uint32_t pay_len;
        if (flags & 0x10) { pay_len = data[i++]; }
        else {
            if (i+4 > len) break;
            pay_len = ((uint32_t)data[i]<<24)|((uint32_t)data[i+1]<<16)
                     |((uint32_t)data[i+2]<<8)|data[i+3];
            i += 4;
        }
        if (flags & 0x08) { if (i>=len) break; i += data[i] + 1; }

        char type_str[48] = {};
        size_t ct = type_len < 47 ? type_len : 47;
        if (i + ct > len) break;
        memcpy(type_str, data+i, ct); i += type_len;

        if (i + pay_len > len) break;
        const char *payload = (const char *)(data + i);
        i += pay_len;

        // Match: "solvare:sign_request"
        if (strstr(type_str, "sign_request")) {
            char pay[256] = {};
            size_t cp = pay_len < 255 ? pay_len : 255;
            memcpy(pay, payload, cp);

            memset(&g_nfc_tx, 0, sizeof(g_nfc_tx));
            strncpy(g_nfc_tx.from, "?", sizeof(g_nfc_tx.from)-1);
            strncpy(g_nfc_tx.to,   "?", sizeof(g_nfc_tx.to)-1);

            // Parse "tx_bytes":"<base64>"
            char *p = strstr(pay, "\"tx_bytes\"");
            if (p) {
                p = strchr(p+10, '"'); if (p) p++; // start of value
                if (p) {
                    char *end = strchr(p, '"');
                    size_t b64len = end ? (size_t)(end-p) : 0;
                    if (b64len > 0)
                        g_nfc_tx.tx_len = (uint16_t)b64_decode(p, b64len,
                                           g_nfc_tx.tx_bytes, NFC_TX_BUFFER_LEN);
                }
            }
            g_nfc_tx.valid = true;
            ESP_LOGI(TAG, "sign_request: %u tx bytes", g_nfc_tx.tx_len);
            found = true;

        } else if (strstr(type_str, "key_import")) {
            if (pay_len >= 32) {
                memcpy(g_nfc_key_import.seed, payload, 32);
                g_nfc_key_import.valid = true;
                found = true;
            }
        }

        if (flags & 0x40) break; // ME bit
    }
    return found;
}

bool hal_nfc_process_ndef(void)
{
    if (!s_ready) return false;
    uint8_t buf[64] = {};

    for (int page = 4; page < 20 && (page-4)*4 < 64; page++) {
        uint8_t cmd[] = {
            0x00,0x00,0xFF,0x05,0xFB,
            0xD4,0x40,0x01,0x30,(uint8_t)page,
            (uint8_t)((0x100-(0xD4+0x40+0x01+0x30+page)) & 0xFF),0x00
        };
        i2c_master_write_to_device(I2C_PORT, PN532_ADDR, cmd, sizeof(cmd), pdMS_TO_TICKS(50));
        vTaskDelay(pdMS_TO_TICKS(10));
        uint8_t resp[20] = {};
        i2c_master_read_from_device(I2C_PORT, PN532_ADDR, resp, sizeof(resp), pdMS_TO_TICKS(50));
        if (resp[7] != 0x00) break;
        memcpy(buf+(page-4)*4, &resp[8], 4);
    }

    for (int k = 0; k < 60; k++) {
        if (buf[k] == 0x03) {
            uint8_t ndef_len = buf[k+1];
            if (k+2+(int)ndef_len <= 64)
                return parse_ndef(buf+k+2, ndef_len);
            break;
        }
    }
    return false;
}

// ── Write sign response — JSON format Android expects ────────────────────
// Android parseSignResponse: {"version":1,"signature":"<base64>"}

bool hal_nfc_write_sign_response(const uint8_t sig[64], const char *nonce)
{
    if (!s_ready) return false;
    (void)nonce;

    char sig_b64[96] = {};
    b64_encode(sig, 64, sig_b64, sizeof(sig_b64));

    char json[140];
    int jlen = snprintf(json, sizeof(json),
                        "{\"version\":1,\"signature\":\"%s\"}", sig_b64);
    if (jlen <= 0 || jlen >= (int)sizeof(json)) return false;

    const char *tname = "solvare:sign_response";
    uint8_t tlen = (uint8_t)strlen(tname);
    uint8_t plen = (uint8_t)jlen;

    uint8_t msg[200] = {};
    uint8_t idx = 0;
    msg[idx++] = 0x03;
    msg[idx++] = (uint8_t)(3 + tlen + plen);
    msg[idx++] = 0xD4; msg[idx++] = tlen; msg[idx++] = plen;
    memcpy(msg+idx, tname, tlen); idx += tlen;
    memcpy(msg+idx, json,  plen); idx += plen;
    msg[idx++] = 0xFE;

    for (uint8_t pg = 0; pg < (idx+3)/4; pg++) {
        uint8_t pd[4] = {};
        for (int j = 0; j < 4 && pg*4+j < idx; j++) pd[j] = msg[pg*4+j];
        uint8_t wr[] = {
            0x00,0x00,0xFF,0x07,0xF9,
            0xD4,0x40,0x01,0xA2,(uint8_t)(4+pg),
            pd[0],pd[1],pd[2],pd[3],
            (uint8_t)((0x100-(0xD4+0x40+0x01+0xA2+(4+pg)+pd[0]+pd[1]+pd[2]+pd[3])) & 0xFF),0x00
        };
        i2c_master_write_to_device(I2C_PORT, PN532_ADDR, wr, sizeof(wr), pdMS_TO_TICKS(50));
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ESP_LOGI(TAG, "sign_response written");
    return true;
}

// ── Write solvare:wallet NDEF — Android can read watch pubkey ─────────────
// Android parseWalletMessage: {"version":1,"pubkey":"<string>","network":"..."}

bool hal_nfc_write_wallet_ndef(const uint8_t pubkey[32])
{
    if (!s_ready) return false;

    char pub_hex[65] = {};
    for (int i = 0; i < 32; i++) snprintf(pub_hex+i*2, 3, "%02X", pubkey[i]);

    char json[128];
    int jlen = snprintf(json, sizeof(json),
                        "{\"version\":1,\"pubkey\":\"%s\",\"network\":\"mainnet\"}", pub_hex);
    if (jlen <= 0) return false;

    const char *tname = "solvare:wallet";
    uint8_t tlen = (uint8_t)strlen(tname);
    uint8_t plen = (uint8_t)jlen;

    uint8_t msg[200] = {};
    uint8_t idx = 0;
    msg[idx++] = 0x03;
    msg[idx++] = (uint8_t)(3 + tlen + plen);
    msg[idx++] = 0xD4; msg[idx++] = tlen; msg[idx++] = plen;
    memcpy(msg+idx, tname, tlen); idx += tlen;
    memcpy(msg+idx, json,  plen); idx += plen;
    msg[idx++] = 0xFE;

    for (uint8_t pg = 0; pg < (idx+3)/4; pg++) {
        uint8_t pd[4] = {};
        for (int j = 0; j < 4 && pg*4+j < idx; j++) pd[j] = msg[pg*4+j];
        uint8_t wr[] = {
            0x00,0x00,0xFF,0x07,0xF9,
            0xD4,0x40,0x01,0xA2,(uint8_t)(4+pg),
            pd[0],pd[1],pd[2],pd[3],
            (uint8_t)((0x100-(0xD4+0x40+0x01+0xA2+(4+pg)+pd[0]+pd[1]+pd[2]+pd[3])) & 0xFF),0x00
        };
        i2c_master_write_to_device(I2C_PORT, PN532_ADDR, wr, sizeof(wr), pdMS_TO_TICKS(50));
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return true;
}
