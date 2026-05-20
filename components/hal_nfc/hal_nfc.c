#include "hal_nfc.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "hal_nfc";
static bool s_ready = false;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_pn532_dev = NULL;
static TaskHandle_t s_nfc_task = NULL;
static volatile bool s_service_enabled = true;
static volatile bool s_service_busy = false;
static uint8_t s_wallet_pubkey[32] = {};
static volatile bool s_wallet_pubkey_valid = false;

nfc_tx_payload_t  g_nfc_tx         = {};
nfc_key_import_t  g_nfc_key_import = {};
nfc_sync_status_t g_nfc_sync       = {.event = NFC_SYNC_IDLE};

static uint8_t s_target_ndef_file[1024] = {};
static size_t  s_target_ndef_file_len   = 2;
static bool    s_target_response_pending = false;
static const uint8_t s_fallback_wallet_pubkey[32] = {0};

#define I2C_PORT   I2C_NUM_0
#define PIN_SDA    5
#define PIN_SCL    6
#define PN532_ADDR 0x24

static const uint8_t ACK_FRAME[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};

static bool parse_ndef(const uint8_t *data, size_t len);
static size_t b64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_max);
static bool json_string_value(const char *json, const char *key, char *out, size_t out_max);
static bool json_u64_value(const char *json, const char *key, uint64_t *out);

static esp_err_t pn532_i2c_write(const uint8_t *data, size_t len, int timeout_ms)
{
    if (!s_pn532_dev) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit(s_pn532_dev, data, len, timeout_ms);
}

static esp_err_t pn532_i2c_read(uint8_t *data, size_t len, int timeout_ms)
{
    if (!s_pn532_dev) return ESP_ERR_INVALID_STATE;
    return i2c_master_receive(s_pn532_dev, data, len, timeout_ms);
}

static void pn532_i2c_deinit(void)
{
    if (s_pn532_dev) {
        i2c_master_bus_rm_device(s_pn532_dev);
        s_pn532_dev = NULL;
    }
    if (s_i2c_bus) {
        i2c_del_master_bus(s_i2c_bus);
        s_i2c_bus = NULL;
    }
}

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

static bool pn532_wait_ready(uint16_t timeout_ms)
{
    int64_t start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) / 1000 < timeout_ms) {
        uint8_t status = 0;
        if (pn532_i2c_read(&status, 1, 60) == ESP_OK &&
            (status & 0x01)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

static bool pn532_read_ack(uint16_t timeout_ms)
{
    if (!pn532_wait_ready(timeout_ms)) return false;

    uint8_t buf[7] = {};
    if (pn532_i2c_read(buf, sizeof(buf), 120) != ESP_OK) {
        return false;
    }
    if (memcmp(buf + 1, ACK_FRAME, sizeof(ACK_FRAME)) != 0) {
        ESP_LOGW(TAG, "PN532 bad ACK %02X%02X%02X%02X%02X%02X",
                 buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);
        return false;
    }
    return true;
}

static bool pn532_read_response(uint8_t expected_code, uint8_t *out, size_t out_max,
                                size_t *out_len, uint16_t timeout_ms)
{
    if (!pn532_wait_ready(timeout_ms)) return false;

    uint8_t raw[300] = {};
    if (pn532_i2c_read(raw, sizeof(raw), 180) != ESP_OK) {
        return false;
    }

    size_t p = 1; // byte 0 is PN532 I2C ready/status
    while (p < sizeof(raw) && raw[p] == 0x00) p++;
    if (p >= sizeof(raw) || raw[p] != 0xFF) return false;
    p++;
    if (p + 1 >= sizeof(raw)) return false;

    uint8_t len = raw[p++];
    uint8_t lcs = raw[p++];
    if ((uint8_t)(len + lcs) != 0 || len < 2) return false;
    if (p + (size_t)len + 1 > sizeof(raw)) return false;
    if (raw[p] != 0xD5 || raw[p + 1] != expected_code) {
        ESP_LOGW(TAG, "PN532 resp 0x%02X want 0x%02X", raw[p + 1], expected_code);
        return false;
    }

    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++) sum = (uint8_t)(sum + raw[p + i]);
    if ((uint8_t)(sum + raw[p + len]) != 0) return false;

    size_t payload_len = len - 2;
    if (out && out_max > 0) {
        if (payload_len > out_max) payload_len = out_max;
        memcpy(out, raw + p + 2, payload_len);
    }
    if (out_len) *out_len = payload_len;
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

    if (pn532_i2c_write(frame, cmd_len + 8, 160) != ESP_OK) {
        ESP_LOGW(TAG, "PN532 write failed cmd=0x%02X", cmd[0]);
        return false;
    }
    if (!pn532_read_ack(220)) {
        ESP_LOGW(TAG, "PN532 ACK timeout cmd=0x%02X", cmd[0]);
        return false;
    }
    if (!pn532_read_response(expected_code, out, out_max, out_len, timeout_ms)) {
        ESP_LOGW(TAG, "PN532 response timeout cmd=0x%02X", cmd[0]);
        return false;
    }
    return true;
}

static bool pn532_write_register(uint16_t addr, uint8_t val)
{
    uint8_t cmd[] = {0x08, (uint8_t)(addr >> 8), (uint8_t)addr, val};
    uint8_t out[4] = {};
    size_t out_len = 0;
    bool ok = pn532_cmd(cmd, sizeof(cmd), 0x09, out, sizeof(out), &out_len, 300);
    if (!ok || (out_len >= 1 && out[0] != 0x00)) {
        ESP_LOGW(TAG, "PN532 WriteRegister 0x%04X=0x%02X failed", addr, val);
        return false;
    }
    return true;
}

static bool pn532_rf_configuration(uint8_t item, const uint8_t *data, size_t data_len)
{
    uint8_t cmd[17] = {};
    if (data_len > 15) data_len = 15;
    cmd[0] = 0x32;
    cmd[1] = item;
    if (data && data_len > 0) memcpy(cmd + 2, data, data_len);
    return pn532_cmd(cmd, data_len + 2, 0x33, NULL, 0, NULL, 300);
}

static void pn532_set_max_rf_power(void)
{
    pn532_write_register(0x6311, 0x3F); // CIU_CWGsP
    pn532_write_register(0x6312, 0x83); // enable TX1 + TX2 antenna drivers
    pn532_write_register(0x6309, 0x3F); // CIU_GsNOn
    uint8_t retries[] = {0xFF, 0x01, 0x05};
    pn532_rf_configuration(0x05, retries, sizeof(retries));
}

static void sync_event(nfc_sync_event_t event, const char *message)
{
    g_nfc_sync.event = event;
    g_nfc_sync.counter++;
    g_nfc_sync.target_active = (event == NFC_SYNC_PHONE_NEAR);
    if (message) {
        snprintf(g_nfc_sync.message, sizeof(g_nfc_sync.message), "%s", message);
    } else {
        g_nfc_sync.message[0] = '\0';
    }
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

static bool build_ndef_file(const uint8_t *ndef, size_t ndef_len, uint8_t *out, size_t out_max, size_t *out_len)
{
    if (!ndef || !out || ndef_len + 2 > out_max || ndef_len > 0xFFFF) return false;
    out[0] = (uint8_t)(ndef_len >> 8);
    out[1] = (uint8_t)ndef_len;
    memcpy(out + 2, ndef, ndef_len);
    if (out_len) *out_len = ndef_len + 2;
    return true;
}

static bool build_wallet_ndef_file(const uint8_t pubkey[32], uint8_t *out, size_t out_max, size_t *out_len)
{
    char pub_b58[48] = {};
    if (!base58_encode(pubkey, 32, pub_b58, sizeof(pub_b58))) return false;

    char json[128];
    int jlen = snprintf(json, sizeof(json),
                        "{\"version\":1,\"pubkey\":\"%s\",\"network\":\"devnet\"}", pub_b58);
    if (jlen <= 0 || jlen >= (int)sizeof(json)) return false;

    uint8_t ndef[260] = {};
    size_t ndef_len = 0;
    if (!build_external_ndef("solwear:wallet", json, ndef, sizeof(ndef), &ndef_len)) return false;
    return build_ndef_file(ndef, ndef_len, out, out_max, out_len);
}

static bool build_sign_response_ndef_file(const uint8_t sig[64], const char *session_id,
                                          uint8_t *out, size_t out_max, size_t *out_len)
{
    char sig_b64[96] = {};
    b64_encode(sig, 64, sig_b64, sizeof(sig_b64));

    char json[200];
    int jlen = snprintf(json, sizeof(json),
                        "{\"version\":1,\"signature\":\"%s\",\"session_id\":\"%s\"}",
                        sig_b64, session_id ? session_id : "");
    if (jlen <= 0 || jlen >= (int)sizeof(json)) return false;

    uint8_t ndef[260] = {};
    size_t ndef_len = 0;
    if (!build_external_ndef("solwear:sign_response", json, ndef, sizeof(ndef), &ndef_len)) return false;
    return build_ndef_file(ndef, ndef_len, out, out_max, out_len);
}

static bool pn532_tg_get_data(uint8_t *data, size_t data_max, size_t *data_len)
{
    const uint8_t cmd[] = {0x86};
    uint8_t out[270] = {};
    size_t out_len = 0;
    if (!pn532_cmd(cmd, sizeof(cmd), 0x87, out, sizeof(out), &out_len, 220)) return false;
    if (out_len < 1 || out[0] != 0x00) return false;
    size_t n = out_len - 1;
    if (data && data_max > 0) {
        if (n > data_max) n = data_max;
        memcpy(data, out + 1, n);
    }
    if (data_len) *data_len = n;
    return true;
}

static bool pn532_tg_set_data(const uint8_t *data, size_t data_len)
{
    if (!data || data_len + 1 > 263) return false;
    uint8_t cmd[264] = {};
    uint8_t out[8] = {};
    size_t out_len = 0;
    cmd[0] = 0x8E;
    memcpy(cmd + 1, data, data_len);
    if (!pn532_cmd(cmd, data_len + 1, 0x8F, out, sizeof(out), &out_len, 220)) return false;
    return out_len >= 1 && out[0] == 0x00;
}

static bool pn532_tg_init_as_target(uint16_t timeout_ms)
{
    const uint8_t cmd[] = {
        0x8C,
        0x05,              // PICC only, passive only
        0x04, 0x00,        // SENS_RES
        0xA5, 0xB6, 0xC7,  // NFCID1t (PN532 uses 3 bytes here)
        0x20,              // SEL_RES: ISO/IEC 14443-4 compliant
        0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,
        0,0,               // FeliCa params padding
        0,0,0,0,0,0,0,0,0,0, // NFCID3t
        0x00,              // no general bytes
        0x00               // no historical bytes
    };
    uint8_t out[32] = {};
    size_t out_len = 0;
    return pn532_cmd(cmd, sizeof(cmd), 0x8D, out, sizeof(out), &out_len, timeout_ms);
}

static size_t target_apdu_response(const uint8_t *apdu, size_t apdu_len, uint8_t *resp, size_t resp_max,
                                   bool *request_written, bool *response_read)
{
    static const uint8_t cc_file[] = {
        0x00, 0x0F,
        0x20,
        0x00, 0x3B,
        0x00, 0x34,
        0x04, 0x06,
        0xE1, 0x04,
        0x04, 0x00,
        0x00,
        0x00
    };
    static int selected_file = 0;
    const uint8_t sw_ok[] = {0x90, 0x00};
    const uint8_t sw_not_found[] = {0x6A, 0x82};
    const uint8_t sw_wrong[] = {0x6B, 0x00};
    const uint8_t sw_unsupported[] = {0x6A, 0x81};

    if (!apdu || apdu_len < 4 || !resp || resp_max < 2) {
        memcpy(resp, sw_wrong, 2);
        return 2;
    }

    uint8_t ins = apdu[1];
    uint8_t p1 = apdu[2];
    uint8_t p2 = apdu[3];
    uint8_t lc = apdu_len > 4 ? apdu[4] : 0;

    if (ins == 0xA4) {
        if (p1 == 0x04 && apdu_len >= 12 && lc == 0x07 &&
            memcmp(apdu + 5, (uint8_t[]){0xD2,0x76,0x00,0x00,0x85,0x01,0x01}, 7) == 0) {
            selected_file = 0;
            memcpy(resp, sw_ok, 2);
            return 2;
        }
        if (p1 == 0x00 && p2 == 0x0C && apdu_len >= 7 && lc == 0x02) {
            uint16_t file_id = ((uint16_t)apdu[5] << 8) | apdu[6];
            if (file_id == 0xE103) {
                selected_file = 0xE103;
                memcpy(resp, sw_ok, 2);
                return 2;
            }
            if (file_id == 0xE104) {
                selected_file = 0xE104;
                memcpy(resp, sw_ok, 2);
                return 2;
            }
        }
        memcpy(resp, sw_not_found, 2);
        return 2;
    }

    if (ins == 0xB0) {
        uint16_t off = ((uint16_t)p1 << 8) | p2;
        const uint8_t *file = NULL;
        size_t file_len = 0;
        if (selected_file == 0xE103) {
            file = cc_file;
            file_len = sizeof(cc_file);
        } else if (selected_file == 0xE104) {
            file = s_target_ndef_file;
            file_len = s_target_ndef_file_len;
        } else {
            memcpy(resp, sw_not_found, 2);
            return 2;
        }
        if (off >= file_len) {
            memcpy(resp, sw_wrong, 2);
            return 2;
        }
        uint8_t le = apdu_len > 4 ? apdu[apdu_len - 1] : 0;
        size_t n = le == 0 ? 256 : le;
        if (n > file_len - off) n = file_len - off;
        if (n + 2 > resp_max) n = resp_max - 2;
        memcpy(resp, file + off, n);
        memcpy(resp + n, sw_ok, 2);
        if (selected_file == 0xE104 && s_target_response_pending && off == 0 && response_read) {
            *response_read = true;
        }
        return n + 2;
    }

    if (ins == 0xD6) {
        if (selected_file != 0xE104 || apdu_len < 5) {
            memcpy(resp, sw_not_found, 2);
            return 2;
        }
        uint16_t off = ((uint16_t)p1 << 8) | p2;
        size_t data_len = lc;
        if (apdu_len < 5 + data_len || off + data_len > sizeof(s_target_ndef_file)) {
            memcpy(resp, sw_wrong, 2);
            return 2;
        }
        memcpy(s_target_ndef_file + off, apdu + 5, data_len);
        if (off + data_len > s_target_ndef_file_len) s_target_ndef_file_len = off + data_len;

        uint16_t ndef_len = ((uint16_t)s_target_ndef_file[0] << 8) | s_target_ndef_file[1];
        if (ndef_len > 0 && 2 + ndef_len <= s_target_ndef_file_len) {
            if (parse_ndef(s_target_ndef_file + 2, ndef_len) && request_written) {
                *request_written = true;
            }
        }
        memcpy(resp, sw_ok, 2);
        return 2;
    }

    memcpy(resp, sw_unsupported, 2);
    return 2;
}

bool hal_nfc_emulate_wallet_target(const uint8_t pubkey[32], uint16_t timeout_ms)
{
    if (!s_ready) return false;

    if (!s_target_response_pending) {
        const uint8_t *tag_pubkey = pubkey ? pubkey : s_fallback_wallet_pubkey;
        if (!build_wallet_ndef_file(tag_pubkey, s_target_ndef_file, sizeof(s_target_ndef_file), &s_target_ndef_file_len)) {
            sync_event(NFC_SYNC_ERROR, "NFC wallet build failed");
            return false;
        }
    }

    if (!pn532_tg_init_as_target(timeout_ms)) {
        g_nfc_sync.target_active = false;
        return false;
    }

    sync_event(NFC_SYNC_PHONE_NEAR, "Phone touched");
    bool request_written = false;
    bool response_read = false;

    for (int i = 0; i < 18; i++) {
        uint8_t apdu[260] = {};
        size_t apdu_len = 0;
        if (!pn532_tg_get_data(apdu, sizeof(apdu), &apdu_len)) break;

        uint8_t resp[270] = {};
        size_t resp_len = target_apdu_response(apdu, apdu_len, resp, sizeof(resp), &request_written, &response_read);
        if (!pn532_tg_set_data(resp, resp_len)) break;

        if (request_written) {
            sync_event(NFC_SYNC_SIGN_REQUEST, "Sign request received");
            break;
        }
    }

    if (response_read && s_target_response_pending) {
        s_target_response_pending = false;
        s_target_ndef_file_len = 2;
        memset(s_target_ndef_file, 0, sizeof(s_target_ndef_file));
        sync_event(NFC_SYNC_SIGN_RESPONSE, "Signature sent");
    } else if (!request_written && !response_read) {
        sync_event(NFC_SYNC_WALLET_SHARED, "Open phone to share");
    }
    g_nfc_sync.target_active = false;
    return true;
}

bool hal_nfc_set_sign_response_target(const uint8_t sig[64], const char *nonce)
{
    if (!sig) return false;
    if (!build_sign_response_ndef_file(sig, nonce, s_target_ndef_file, sizeof(s_target_ndef_file), &s_target_ndef_file_len)) {
        sync_event(NFC_SYNC_ERROR, "NFC signature build failed");
        return false;
    }
    s_target_response_pending = true;
    sync_event(NFC_SYNC_SIGN_RESPONSE, "Signature ready");
    return true;
}

void hal_nfc_set_wallet_pubkey(const uint8_t pubkey[32])
{
    if (!pubkey) {
        s_wallet_pubkey_valid = false;
        memset(s_wallet_pubkey, 0, sizeof(s_wallet_pubkey));
        return;
    }
    memcpy(s_wallet_pubkey, pubkey, sizeof(s_wallet_pubkey));
    s_wallet_pubkey_valid = true;
}

void hal_nfc_set_service_enabled(bool enabled)
{
    s_service_enabled = enabled;
}

static void nfc_service_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "NFC Type 4 target service task started");
    while (true) {
        if (!s_service_enabled || !s_ready) {
            vTaskDelay(pdMS_TO_TICKS(80));
            continue;
        }

        uint8_t pubkey[32] = {};
        const uint8_t *pubkey_ptr = NULL;
        if (s_wallet_pubkey_valid) {
            memcpy(pubkey, s_wallet_pubkey, sizeof(pubkey));
            pubkey_ptr = pubkey;
        }

        s_service_busy = true;
        hal_nfc_emulate_wallet_target(pubkey_ptr, 600);
        s_service_busy = false;
        vTaskDelay(pdMS_TO_TICKS(15));
    }
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
        pn532_i2c_write(wr, sizeof(wr), 50);
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

static bool json_string_value(const char *json, const char *key, char *out, size_t out_max)
{
    if (!json || !key || !out || out_max == 0) return false;
    out[0] = '\0';

    char needle[40] = {};
    int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (n <= 0 || n >= (int)sizeof(needle)) return false;

    const char *p = strstr(json, needle);
    if (!p) return false;
    p += n;
    p = strchr(p, ':');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return false;
    p++;

    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_max) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

static bool json_u64_value(const char *json, const char *key, uint64_t *out)
{
    if (!json || !key || !out) return false;

    char needle[40] = {};
    int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (n <= 0 || n >= (int)sizeof(needle)) return false;

    const char *p = strstr(json, needle);
    if (!p) return false;
    p += n;
    p = strchr(p, ':');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;

    uint64_t v = 0;
    bool any = false;
    while (*p >= '0' && *p <= '9') {
        any = true;
        v = v * 10 + (uint64_t)(*p - '0');
        p++;
    }
    if (!any) return false;
    *out = v;
    return true;
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

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK) return false;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PN532_ADDR,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_pn532_dev);
    if (err != ESP_OK) {
        pn532_i2c_deinit();
        return false;
    }

    uint8_t fw[8] = {};
    size_t fw_len = 0;
    const uint8_t get_firmware[] = {0x02};
    if (pn532_cmd(get_firmware, sizeof(get_firmware), 0x03,
                  fw, sizeof(fw), &fw_len, 120) && fw_len >= 4) {
        const uint8_t sam_config[] = {0x14,0x01,0x14,0x01};
        if (!pn532_cmd(sam_config, sizeof(sam_config), 0x15, NULL, 0, NULL, 120)) {
            ESP_LOGW(TAG, "PN532 SAMConfiguration failed");
            pn532_i2c_deinit();
            return false;
        }
        pn532_set_max_rf_power();
        s_ready = true;
        if (!s_nfc_task) {
            xTaskCreate(nfc_service_task, "nfc_type4", 4096, NULL, 5, &s_nfc_task);
        }
        ESP_LOGI(TAG, "PN532 v%d.%d", fw[1], fw[2]);
        return true;
    }
    ESP_LOGW(TAG, "PN532 not found");
    pn532_i2c_deinit();
    return false;
}

void hal_nfc_shutdown(void)
{
    if (!s_ready) return;
    s_service_enabled = false;
    for (int i = 0; i < 12 && s_service_busy; i++) {
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    pn532_i2c_deinit();
    s_ready = false;
}

bool hal_nfc_is_ready(void) { return s_ready; }

bool hal_nfc_wait_tag(uint16_t timeout_ms, nfc_tag_t *tag)
{
    if (!s_ready || !tag) return false;
    const uint8_t cmd[] = {0x4A,0x01,0x00};
    uint8_t resp[32] = {};
    size_t resp_len = 0;
    if (!pn532_cmd(cmd, sizeof(cmd), 0x4B, resp, sizeof(resp), &resp_len, timeout_ms)) {
        return false;
    }
    if (resp_len < 7 || resp[0] == 0) return false;
    uint8_t uid_len = resp[5];
    if (resp_len < (size_t)(6 + uid_len)) return false;
    tag->uid_len = uid_len > 7 ? 7 : uid_len;
    memcpy(tag->uid, &resp[6], tag->uid_len);
    return true;
}

// ── NDEF parser — aligned with Android NdefProtocol.kt ───────────────────
// Android External Type records: type = "solwear:sign_request"
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

        // Match: "solwear:sign_request" and legacy "solvare:sign_request"
        if (strstr(type_str, "sign_request")) {
            char pay[512] = {};
            size_t cp = pay_len < sizeof(pay) - 1 ? pay_len : sizeof(pay) - 1;
            memcpy(pay, payload, cp);

            memset(&g_nfc_tx, 0, sizeof(g_nfc_tx));
            strncpy(g_nfc_tx.from, "?", sizeof(g_nfc_tx.from)-1);
            strncpy(g_nfc_tx.to,   "?", sizeof(g_nfc_tx.to)-1);
            strncpy(g_nfc_tx.network, "devnet", sizeof(g_nfc_tx.network)-1);

            char tx_b64[360] = {};
            if (json_string_value(pay, "tx_bytes", tx_b64, sizeof(tx_b64))) {
                g_nfc_tx.tx_len = (uint16_t)b64_decode(tx_b64, strlen(tx_b64),
                                   g_nfc_tx.tx_bytes, NFC_TX_BUFFER_LEN);
            }
            json_string_value(pay, "from", g_nfc_tx.from, sizeof(g_nfc_tx.from));
            json_string_value(pay, "to", g_nfc_tx.to, sizeof(g_nfc_tx.to));
            json_string_value(pay, "network", g_nfc_tx.network, sizeof(g_nfc_tx.network));
            json_u64_value(pay, "lamports", &g_nfc_tx.lamports);
            json_u64_value(pay, "fee_lamports", &g_nfc_tx.fee_lamports);
            if (!json_string_value(pay, "session_id", g_nfc_tx.nonce, sizeof(g_nfc_tx.nonce))) {
                g_nfc_tx.nonce[0] = '\0';
            }
            g_nfc_tx.valid = true;
            ESP_LOGI(TAG, "sign_request: %u tx bytes session=%s",
                     g_nfc_tx.tx_len, g_nfc_tx.nonce);
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
        pn532_i2c_write(cmd, sizeof(cmd), 50);
        vTaskDelay(pdMS_TO_TICKS(10));
        uint8_t resp[20] = {};
        pn532_i2c_read(resp, sizeof(resp), 50);
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

    char sig_b64[96] = {};
    b64_encode(sig, 64, sig_b64, sizeof(sig_b64));

    char json[200];
    int jlen = snprintf(json, sizeof(json),
                        "{\"version\":1,\"signature\":\"%s\",\"session_id\":\"%s\"}",
                        sig_b64, nonce ? nonce : "");
    if (jlen <= 0 || jlen >= (int)sizeof(json)) return false;

    uint8_t ndef[260] = {};
    size_t ndef_len = 0;
    if (!build_external_ndef("solwear:sign_response", json, ndef, sizeof(ndef), &ndef_len)) {
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
    if (!build_external_ndef("solwear:wallet", json, ndef, sizeof(ndef), &ndef_len)) {
        return false;
    }

    if (type4_write_ndef(ndef, ndef_len)) return true;
    return write_legacy_ntag_ndef(ndef, ndef_len);
}
