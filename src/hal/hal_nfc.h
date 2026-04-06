#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include "config.h"

struct NfcTagInfo {
    uint8_t uid[7];
    uint8_t uidLength;
    bool valid;
};

class HalNfc {
public:
    // No-op now — NFC is lazy-initialized on first use.
    // Keeps API stable so main.cpp can call it harmlessly.
    void init();

    // Lazy init: powers up PN532 and probes for it. Safe to call repeatedly.
    // Returns true on success, false if module is missing or unresponsive.
    bool ensureInit();

    // Power down the PN532 (low power mode) and mark not-ready.
    // Called when NFC app exits so module is off by default.
    void shutdown();

    bool isReady() const { return ready_; }
    bool isInitialized() const { return initialized_; }

    bool waitForTag(uint16_t timeoutMs, NfcTagInfo& tag);
    bool readNdef(char* buffer, size_t maxLen);
    bool writeNdefUrl(const char* url);

private:
    // I2C constructor — IRQ=255 (unused), RST=255 (unused), wire=&Wire
    Adafruit_PN532 nfc_ = Adafruit_PN532(255, 255, &Wire);
    bool ready_ = false;        // PN532 is responsive (firmware version OK)
    bool initialized_ = false;  // ensureInit() was attempted
    bool wireStarted_ = false;  // I2C0 bus is running
};

extern HalNfc nfc;
