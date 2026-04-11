#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"

struct Settings {
    uint8_t brightness = BRIGHTNESS_DEFAULT;
    bool soundEnabled = true;
    bool vibrationEnabled = true;
    uint8_t watchFaceIndex = 0;
    bool timeFormat24h = true;
    uint8_t wallpaperIndex = 0;
    uint16_t stepGoal = DEFAULT_STEP_GOAL;
    float batteryDivider = BATTERY_DIVIDER;  // Per-device calibration
};

struct WalletData {
    char address[64] = "";
    bool hasAddress = false;
};

class Storage {
public:
    static Storage& instance() {
        static Storage inst;
        return inst;
    }

    bool init();

    bool loadSettings(Settings& settings);
    bool saveSettings(const Settings& settings);

    bool loadWallet(WalletData& wallet);
    bool saveWallet(const WalletData& wallet);

    bool saveSteps(uint8_t dayIndex, uint32_t steps);
    bool loadSteps(uint8_t dayIndex, uint32_t& steps);
    bool loadWeekSteps(uint32_t weekSteps[7]);

private:
    Storage() {}
    bool mounted_ = false;
};
