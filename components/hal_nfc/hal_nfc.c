#include "hal_nfc.h"
#include "driver/i2c.h"  // legacy I2C driver, still valid in IDF 6.x via esp_driver_i2c
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
static const char kB58[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

static bool base58_encode(const uint8_t *in, size_t in_len, char *out, size_t out_max)
{
    if (!out || out_max == 0) return false;
    if (!in || in_len == 0) {
        out[0] = '\0';
        return true;
    }

    uint8_t buf[40] = {};
    if (in_len > sizeof(buf)) return false;
    memcpy(buf, in, in_len);

    size_t zeros = 0;
    while (zeros < in_len && buf[zeros] == 0) zeros++;

    char tmp[72] = {};
    size_t tmp_len = 0;
    size_t start = zeros;
    while (start < in_len) {
        uint32_t rem = 0;
        for (size_t i = start; i < in_len; i++) {
            uint32_t v = (rem << 8) | buf[i];
            buf[i] = (uint8_t)(v / 58);
            rem = v % 58;
        }
        if (tmp_len >= sizeof(tmp)) return false;
        tmp[tmp_len++] = kB58[rem];
        while (start < in_len && buf[start] == 0) start++;
    }

    size_t out_len = zeros + tmp_len;
    if (out_len + 1 > out_max) return false;

    size_t p = 0;
    for (size_t i = 0; i < zeros; i++) out[p++] = '1';
    while (tmp_len > 0) out[p++] = tmp[--tmp_len];
    out[p] = '\0';
    return true;
}

static bool pn532_cmd(const uint8_t *cmd, size_t cmd_len,
                      uint8_t expected_code,
                      uint8_t *out, size_t out_max, size_t *out_len,
                      uint16_t timeout_ms)
{
    if (!cmd || cmd_len == 0 || cmd_len > 262) return false;

    uint8_t frame[272] = {};
    uint8_t len = (uint8_t)(cmd_len + 1);
    uint8_t sum = 0xD4;
    frame[0] = 0x00;
    frame[1] = 0x00;
    frame[2] = 0xFF;
    frame[3] = len;
    frame[4] = (uint8_t)(0x100 - len);
    frame[5] = 0xD4;
    for (size_t i = 0; i < cmd_len; i++) {
        frame[6 + i] = cmd[i];
        sum = (uint8_t)(sum + cmd[i]);
    }
    frame[6 + cmd_len] = (uint8_t)(0x100 - sum);
    frame[7 + cmd_len] = 0x00;

    if (i2c_master_write_to_device(I2C_PORT, PN532_ADDR, frame, cmd_len + 8,
                                   pdMS_TO_TICKS(timeout_ms)) != ESP_OK) {
        return false;
    }

    uint8_t raw[300] = {};
    for (int attempt = 0; attempt < 3; attempt++) {
        vTaskDelay(pdMS_TO_TICKS(18 + attempt * 12));
        memset(raw, 0, sizeof(raw));
        if (i2c_master_read_from_device(I2C_PORT, PN532_ADDR, raw, sizeof(raw),
                                        pdMS_TO_TICKS(timeout_ms)) != ESP_OK) {
            continue;
        }

        int p = -1;
        for (int i = 0; i < (int)sizeof(raw) - 5; i++) {
            if (raw[i] == 0x00 && raw[i+1] == 0x00 && raw[i+2] == 0xFF) {
                p = i;
                break;
            }
        }
        if (p < 0) continue;
        if (raw[p+3] == 0x00 && raw[p+4] == 0xFF) continue; // ACK

        uint8_t rlen = raw[p+3];
        if (rlen < 2 || p + 6 + rlen > (int)sizeof(raw)) continue;
        uint8_t *data = &raw[p+6];
        if (data[0] != 0xD5 || data[1] != expected_code) continue;

        size_t payload_len = rlen - 2;
        if (out && out_max > 0) {
            if (payload_len > out_max) payload_len = out_max;
            memcpy(out, data + 2, payload_len);
        }
        if (out_len) *out_len = payload_len;
        return true;
    }
    return false;
}

static bool pn532_in_data_exchange(const uint8_t *apdu, size_t apdu_len,
                                   uint8_t *resp, size_t resp_max, size_t *resp_len)
{
    uint8_t cmd[266] = {};
    uint8_t out[266] = {};
    size_t out_len = 0;
    if (!apdu || apdu_len + 2 > sizeof(cmd)) return false;

    cmd[0] = 0x40;
    cmd[1] = 0x01;
    memcpy(cmd + 2, apdu, apdu_len);

    if (!pn532_cmd(cmd, apdu_len + 2, 0x41, out, sizeof(out), &out_len, 120)) {
        return false;
    }
    if (out_len < 1 || out[0] != 0x00) return false;

    size_t data_len = out_len - 1;
    if (resp && resp_max > 0) {
        if (data_len > resp_max) data_len = resp_max;
        memcpy(resp, out + 1, data_len);
    }
    if (resp_len) *resp_len = data_len;
    return true;
}

static bool apdu_ok(const uint8_t *resp, size_t len)
{
    return len >= 2 && resp[len - 2] == 0x90 && resp[len - 1] == 0x00;
}

static bool type4_select_ndef_file(void)
{
    static const uint8_t select_app[] = {
        0x00,0xA4,0x04,0x00,0x07,0xD2,0x76,0x00,0x00,0x85,0x01,0x01
    };
    static const uint8_t select_file[] = {
        0x00,0xA4,0x00,0x0C,0x02,0xE1,0x04
    };
    uint8_t resp[32] = {};
    size_t len = 0;

    if (!pn532_in_data_exchange(select_app, sizeof(select_app), resp, sizeof(resp), &len) ||
        !apdu_ok(resp, len)) {
        return false;
    }
    memset(resp, 0, sizeof(resp));
    len = 0;
    return pn532_in_data_exchange(select_file, sizeof(select_file), resp, sizeof(resp), &len) &&
           apdu_ok(resp, len);
}

static bool type4_read_binary(uint16_t offset, uint8_t le, uint8_t *data, size_t *data_len)
{
    uint8_t apdu[] = {0x00,0xB0,(uint8_t)(offset >> 8),(uint8_t)offset,le};
    uint8_t resp[270] = {};
    size_t len = 0;
    if (!pn532_in_data_exchange(apdu, sizeof(apdu), resp, sizeof(resp), &len) ||
        !apdu_ok(resp, len)) {
        return false;
    }
    size_t n = len - 2;
    if (data && n > 0) memcpy(data, resp, n);
    if (data_len) *data_len = n;
    return true;
}

static bool type4_update_binary(uint16_t offset, const uint8_t *data, size_t data_len)
{
    if (!data || data_len > 240) return false;
    uint8_t apdu[245] = {};
    uint8_t resp[16] = {};
    size_t len = 0;
    apdu[0] = 0x00;
    apdu[1] = 0xD6;
    apdu[2] = (uint8_t)(offset >> 8);
    apdu[3] = (uint8_t)offset;
    apdu[4] = (uint8_t)data_len;
    memcpy(apdu + 5, data, data_len);
    return pn532_in_data_exchange(apdu, data_len + 5, resp, sizeof(resp), &len) &&
           apdu_ok(resp, len);
}

static bool type4_read_ndef(uint8_t *ndef, size_t ndef_max, size_t *ndef_len)
{
    if (!type4_select_ndef_file()) return false;

    uint8_t hdr[2] = {};
    size_t got = 0;
    if (!type4_read_binary(0, 2, hdr, &got) || got != 2) return false;

    uint16_t len = ((uint16_t)hdr[0] << 8) | hdr[1];
    if (len == 0 || len > ndef_max) return false;

    uint16_t off = 2;
    size_t total = 0;
    while (total < len) {
        uint8_t chunk[240] = {};
        size_t chunk_len = 0;
        uint8_t want = (uint8_t)((len - total) > sizeof(chunk) ? sizeof(chunk) : (len - total));
        if (!type4_read_binary(off, want, chunk, &chunk_len) || chunk_len == 0) return false;
        memcpy(ndef + total, chunk, chunk_len);
        total += chunk_len;
        off += (uint16_t)chunk_len;
    }

    if (ndef_len) *ndef_len = total;
    return true;
}

static bool type4_write_ndef(const uint8_t *ndef, size_t ndef_len)
{
    if (!ndef || ndef_len == 0 || ndef_len > 1024) return false;
    if (!type4_select_ndef_file()) return false;

    uint8_t zero_len[2] = {0x00, 0x00};
    if (!type4_update_binary(0, zero_len, sizeof(zero_len))) return false;

    uint16_t off = 2;
    size_t sent = 0;
    while (sent < ndef_len) {
        size_t chunk_len = (ndef_len - sent) > 220 ? 220 : (ndef_len - sent);
        if (!type4_update_binary(off, ndef + sent, chunk_len)) return false;
        sent += chunk_len;
        off += (uint16_t)chunk_len;
    }

    uint8_t final_len[2] = {(uint8_t)(ndef_len >> 8), (uint8_t)ndef_len};
    return type4_update_binary(0, final_len, sizeof(final_len));
}

static bool build_external_ndef(const char *type, const char *payload,
                                uint8_t *out, size_t out_max, size_t *out_len)
{
    size_t tlen = strlen(type);
    size_t plen = strlen(payload);
    size_t need = 3 + tlen + plen;
    bool short_record = plen <= 255;
    if (!short_record) need += 3;
    if (!out || tlen > 255 || need > out_max) return false;

    size_t idx = 0;
    out[idx++] = short_record ? 0xD4 : 0xC4;
    out[idx++] = (uint8_t)tlen;
    if (short_record) {
        out[idx++] = (uint8_t)plen;
    } else {
        out[idx++] = (uint8_t)(plen >> 24);
        out[idx++] = (uint8_t)(plen >> 16);
        out[idx++] = (uint8_t)(plen >> 8);
        out[idx++] = (uint8_t)plen;
    }
    memcpy(out + idx, type, tlen);
    idx += tlen;
    memcpy(out + idx, payload, plen);
    idx += plen;
    if (out_len) *out_len = idx;
    return true;
}

static bool write_legacy_ntag_ndef(const uint8_t *ndef, size_t ndef_len)
{
    if (!ndef || ndef_len == 0 || ndef_len > 250) return false;
    uint8_t msg[260] = {};
    size_t idx = 0;
    msg[idx++] = 0x03;
    msg[idx++] = (uint8_t)ndef_len;
    memcpy(msg + idx, ndef, ndef_len);
    idx += ndef_len;
    msg[idx++] = 0xFE;

    for (uint8_t pg = 0; pg < (idx + 3) / 4; pg++) {
        uint8_t pd[4] = {};
        for (int j = 0; j < 4 && pg * 4 + j < idx; j++) pd[j] = msg[pg * 4 + j];
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
            char pay[512] = {};
            size_t cp = pay_len < sizeof(pay) - 1 ? pay_len : sizeof(pay) - 1;
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

    uint8_t type4_ndef[512] = {};
    size_t type4_len = 0;
    if (type4_read_ndef(type4_ndef, sizeof(type4_ndef), &type4_len)) {
        return parse_ndef(type4_ndef, type4_len);
    }

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

    uint8_t ndef[180] = {};
    size_t ndef_len = 0;
    if (!build_external_ndef("solvare:sign_response", json, ndef, sizeof(ndef), &ndef_len)) {
        return false;
    }

    if (type4_write_ndef(ndef, ndef_len)) {
        ESP_LOGI(TAG, "sign_response written via Type 4");
        return true;
    }
    if (!write_legacy_ntag_ndef(ndef, ndef_len)) return false;
    ESP_LOGI(TAG, "sign_response written");
    return true;
}

// ── Write solvare:wallet NDEF — Android can read watch pubkey ─────────────
// Android parseWalletMessage: {"version":1,"pubkey":"<string>","network":"..."}

bool hal_nfc_write_wallet_ndef(const uint8_t pubkey[32])
{
    if (!s_ready) return false;

    char pub_b58[48] = {};
    if (!base58_encode(pubkey, 32, pub_b58, sizeof(pub_b58))) {
        return false;
    }

    char json[128];
    int jlen = snprintf(json, sizeof(json),
                        "{\"version\":1,\"pubkey\":\"%s\",\"network\":\"devnet\"}", pub_b58);
    if (jlen <= 0 || jlen >= (int)sizeof(json)) return false;

    uint8_t ndef[180] = {};
    size_t ndef_len = 0;
    if (!build_external_ndef("solvare:wallet", json, ndef, sizeof(ndef), &ndef_len)) {
        return false;
    }

    if (type4_write_ndef(ndef, ndef_len)) return true;
    return write_legacy_ntag_ndef(ndef, ndef_len);
}
