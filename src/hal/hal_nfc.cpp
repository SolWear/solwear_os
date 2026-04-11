#include "hal_nfc.h"

HalNfc nfc;

// Eager init is intentionally a no-op. We do NOT bring up the PN532 at boot
// because: (a) it draws power continuously, (b) Adafruit_PN532::begin() can
// hang for several seconds if the module isn't responding, which previously
// caused boot crashes / watchdog resets. NFC is brought up on demand from
// NfcApp::onCreate() (or wallet transaction flow) via ensureInit().
void HalNfc::init() {
    Serial.println("[HAL] NFC: lazy mode (off until needed)");
}

bool HalNfc::ensureInit() {
    if (initialized_) {
        return ready_;
    }
    initialized_ = true;

    Serial.println("[HAL] NFC: powering up PN532...");

    if (!wireStarted_) {
#if defined(ARDUINO_ARCH_RP2040)
        Wire.setSDA(PIN_NFC_SDA);
        Wire.setSCL(PIN_NFC_SCL);
        Wire.begin();
#else
        Wire.begin(PIN_NFC_SDA, PIN_NFC_SCL);
#endif
        Wire.setClock(100000);  // PN532 prefers 100kHz
        wireStarted_ = true;
    }

    nfc_.begin();
    delay(50);

    uint32_t version = nfc_.getFirmwareVersion();
    if (!version) {
        Serial.println("[HAL] NFC: PN532 not found");
        ready_ = false;
        return false;
    }

    Serial.printf("[HAL] NFC: PN532 firmware v%d.%d\n",
                  (version >> 16) & 0xFF, (version >> 8) & 0xFF);

    // Configure SAM (Secure Access Module) for normal tag reading
    nfc_.SAMConfig();
    ready_ = true;
    return true;
}

void HalNfc::shutdown() {
    if (!initialized_ || !ready_) {
        ready_ = false;
        return;
    }
    Serial.println("[HAL] NFC: powering down");
    // PN532 PowerDown command — wake source bit 0x20 = I2C
    // Adafruit lib doesn't expose this directly; safest portable
    // option is to drop ready_ so we stop polling. Re-init via
    // ensureInit() will reset the chip via SAMConfig().
    ready_ = false;
    initialized_ = false;
}

bool HalNfc::waitForTag(uint16_t timeoutMs, NfcTagInfo& tag) {
    if (!ready_) return false;

    tag.valid = false;
    bool success = nfc_.readPassiveTargetID(PN532_MIFARE_ISO14443A,
                                             tag.uid, &tag.uidLength,
                                             timeoutMs);
    if (success) {
        tag.valid = true;
        return true;
    }
    return false;
}

bool HalNfc::readNdef(char* buffer, size_t maxLen) {
    if (!ready_) return false;

    uint8_t data[128];

    // Try to read page/block 4 (start of NDEF on NTAG/Mifare Ultralight)
    if (!nfc_.ntag2xx_ReadPage(4, data)) {
        return false;
    }

    // Read pages 4-15 for NDEF content
    uint8_t ndefBuf[48];
    for (int page = 4; page < 16; page++) {
        if (!nfc_.ntag2xx_ReadPage(page, ndefBuf + (page - 4) * 4)) {
            break;
        }
    }

    // Find NDEF URL record
    for (size_t i = 0; i < 44; i++) {
        if (ndefBuf[i] == 0xD1 && ndefBuf[i + 3] == 0x55) {
            uint8_t payloadLen = ndefBuf[i + 2] - 1;
            uint8_t uriCode = ndefBuf[i + 4];
            size_t copyLen = (payloadLen < maxLen - 1) ? payloadLen : maxLen - 1;

            const char* prefix = "";
            switch (uriCode) {
                case 0x00: prefix = ""; break;
                case 0x01: prefix = "http://www."; break;
                case 0x02: prefix = "https://www."; break;
                case 0x03: prefix = "http://"; break;
                case 0x04: prefix = "https://"; break;
            }

            size_t prefixLen = strlen(prefix);
            if (prefixLen + copyLen >= maxLen) copyLen = maxLen - prefixLen - 1;

            memcpy(buffer, prefix, prefixLen);
            memcpy(buffer + prefixLen, ndefBuf + i + 5, copyLen);
            buffer[prefixLen + copyLen] = '\0';
            return true;
        }
    }

    return false;
}

bool HalNfc::writeNdefUrl(const char* url) {
    if (!ready_) return false;

    uint8_t uriCode = 0x00;
    const char* payload = url;

    if (strncmp(url, "https://www.", 12) == 0) {
        uriCode = 0x02; payload = url + 12;
    } else if (strncmp(url, "http://www.", 11) == 0) {
        uriCode = 0x01; payload = url + 11;
    } else if (strncmp(url, "https://", 8) == 0) {
        uriCode = 0x04; payload = url + 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        uriCode = 0x03; payload = url + 7;
    }

    uint8_t payloadLen = strlen(payload) + 1;

    uint8_t ndefMsg[64];
    uint8_t idx = 0;
    ndefMsg[idx++] = 0x03;
    ndefMsg[idx++] = payloadLen + 3;
    ndefMsg[idx++] = 0xD1;
    ndefMsg[idx++] = 0x01;
    ndefMsg[idx++] = payloadLen;
    ndefMsg[idx++] = 0x55;
    ndefMsg[idx++] = uriCode;
    memcpy(ndefMsg + idx, payload, strlen(payload));
    idx += strlen(payload);
    ndefMsg[idx++] = 0xFE;

    for (uint8_t page = 0; page < (idx + 3) / 4; page++) {
        uint8_t pageData[4] = {0};
        for (int j = 0; j < 4 && (page * 4 + j) < idx; j++) {
            pageData[j] = ndefMsg[page * 4 + j];
        }
        if (!nfc_.ntag2xx_WritePage(4 + page, pageData)) {
            return false;
        }
    }

    return true;
}
